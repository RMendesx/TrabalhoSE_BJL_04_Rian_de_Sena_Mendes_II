/*--------------------------------------------------------------------------------------------------------------------------------------------*/
/*IMPORTS E DIFINES*/
/*--------------------------------------------------------------------------------------------------------------------------------------------*/
#include <stdlib.h>          // funções para realizar várias operações, incluindo alocação de memória dinâmica (malloc)
#include <stdio.h>           // Biblioteca padrão para entrada e saída
#include <string.h>          // Biblioteca manipular strings
#include "pico/stdlib.h"     // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "hardware/adc.h"    // Biblioteca da Raspberry Pi Pico para manipulação do conversor ADC
#include "hardware/pwm.h"    // Biblioteca para controle de PWM (modulação por largura de pulso)
#include "pico/cyw43_arch.h" // Biblioteca para arquitetura Wi-Fi da Pico com CYW43
#include "lwip/pbuf.h"       // Lightweight IP stack - manipulação de buffers de pacotes de rede
#include "lwip/tcp.h"        // Lightweight IP stack - fornece funções e estruturas para trabalhar com o protocolo TCP
#include "lwip/netif.h"      // Lightweight IP stack - fornece funções e estruturas para trabalhar com interfaces de rede (netif)
#include "lib/buzzer.h"      // Biblioteca para controle de buzzer
#include "lib/ssd1306.h"     // Biblioteca para controle de display OLED
#include "lib/font.h"        // Biblioteca para fonte do display OLED

#define WIFI_SSID "XXX"            // Nome da rede Wi-Fi
#define WIFI_PASSWORD "XXXXXXXX" // Senha da rede Wi-Fi

#define BUTTON_A 5                    // GPIO5 - Botão
#define LED_PIN CYW43_WL_GPIO_LED_PIN // GPIO do CI CYW43
#define LED_GREEN_PIN 11              // GPIO11 - LED verde
#define LED_BLUE_PIN 12               // GPIO12 - LED azul
#define LED_RED_PIN 13                // GPIO13 - LED vermelho
#define BUZZER1_PIN 10                // GPIO10 - Buzzer 1
#define BUZZER2_PIN 21                // GPIO21 - Buzzer 2
#define I2C_PORT i2c1                 // I2C1
#define I2C_SDA 14                    // GPIO14 - SDA
#define I2C_SCL 15                    // GPIO15 - SCL
#define endereco 0x3C                 // Endereço do display OLED

bool alarm_active = false;
bool alarm_verification = false;

/*--------------------------------------------------------------------------------------------------------------------------------------------*/
/*BOOTSEL*/
/*--------------------------------------------------------------------------------------------------------------------------------------------*/

#include "pico/bootrom.h"
#define BUTTON_B 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    reset_usb_boot(0, 0);
}

/*--------------------------------------------------------------------------------------------------------------------------------------------*/
/*FUNÇÕES*/
/*--------------------------------------------------------------------------------------------------------------------------------------------*/

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_led_bitdog(void);

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_button_bitdog(void);

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Tratamento do request do usuário
void user_request(char **request);

// Função para iniciar o alarme
void start_alarm();

// Função para parar o alarme
void stop_alarm();

/*--------------------------------------------------------------------------------------------------------------------------------------------*/
/*MAIN*/
/*--------------------------------------------------------------------------------------------------------------------------------------------*/

int main()
{
    // Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();

    // Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
    gpio_led_bitdog();

    // Inicializar os Pinos GPIO para acionamento dos Botões da BitDogLab
    gpio_button_bitdog();

    // Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    // GPIO do CI CYW43 em nível baixo
    cyw43_arch_gpio_put(LED_PIN, 0);

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000))
    {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");

    // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default)
    {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    // vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");

    /*--------------------------------------------------------------------------*/
    /*INICIALIZAÇÃO DO I2C E DISPLAY*/
    /*--------------------------------------------------------------------------*/

    i2c_init(I2C_PORT, 400 * 1000); // I2C Initialisation. Using it at 400Khz.

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_pull_up(I2C_SDA);                                        // Pull up the data line
    gpio_pull_up(I2C_SCL);                                        // Pull up the clock line
    ssd1306_t ssd;                                                // Inicializa a estrutura do display
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd);                                         // Configura o display
    ssd1306_send_data(&ssd);                                      // Envia os dados para o display

    ssd1306_fill(&ssd, false); // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_send_data(&ssd);   // Envia os dados para o display

    bool cor = true;

    ssd1306_fill(&ssd, !cor);                      // Limpa o display
    ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);  // Desenha um retângulo
    ssd1306_draw_string(&ssd, "SEGURANCA", 29, 6); // Desenha uma string
    ssd1306_line(&ssd, 3, 15, 123, 15, cor);       // Desenha uma linha
    ssd1306_send_data(&ssd);                       // Atualiza o display
    stop_alarm();                                  // Inicia com o alarme desativado

    /*--------------------------------------------------------------------------*/
    /*LOOP INFINITO*/
    /*--------------------------------------------------------------------------*/

    while (true)
    {
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo

        ssd1306_draw_string(&ssd, " SEM INTRUSOS ", 8, 36); // Desenha uma string
        ssd1306_send_data(&ssd);                            // Envia os dados para o display

        buzzer_off(BUZZER1_PIN); // Desativa o buzzer 1
        buzzer_off(BUZZER2_PIN); // Desativa o buzzer 2

        if (alarm_verification)
        {
            // Espera o botão A (GPIO 5) ser pressionado
            printf("Aguardando pressionamento do botão A para ativar o buzzer...\n");
            while (gpio_get(BUTTON_A))
            {
                sleep_ms(10); // Verifica a cada 10 ms
            }

            // Quando o botão for pressionado, toca o alarme
            printf("Botão precionado, tocando alarme!\n");
            alarm_active = true; // Ativa o alarme
        }

        while (alarm_active) // Enquanto o alarme estiver ativo
        {

            ssd1306_draw_string(&ssd, "  .INTRUSO.  ", 12, 36); // Desenha uma string
            ssd1306_send_data(&ssd);                            // Envia os dados para o display

            buzzer_on(BUZZER1_PIN, 2000); // Ativa o buzzer 1
            buzzer_on(BUZZER2_PIN, 2000); // Ativa o buzzer 2

            gpio_put(LED_GREEN_PIN, 0); // Desliga o LED verde
            gpio_put(LED_BLUE_PIN, 1);  // Liga o LED azul
            gpio_put(LED_RED_PIN, 0);   // Desliga o LED vermelho

            sleep_ms(200); // Aguarda 200ms

            /*--------------------------------------------------------------------------*/

            ssd1306_draw_string(&ssd, " ..INTRUSO.. ", 12, 36); // Desenha uma string
            ssd1306_send_data(&ssd);                            // Envia os dados para o display

            buzzer_off(BUZZER1_PIN); // Desativa o buzzer 1
            buzzer_off(BUZZER2_PIN); // Desativa o buzzer 2

            gpio_put(LED_RED_PIN, 1);  // Liga o LED vermelho
            gpio_put(LED_BLUE_PIN, 0); // Desliga o LED azul

            sleep_ms(100); // Aguarda 100ms

            /*--------------------------------------------------------------------------*/

            ssd1306_draw_string(&ssd, "...INTRUSO...", 12, 36); // Desenha uma string
            ssd1306_send_data(&ssd);                            // Envia os dados para o display

            sleep_ms(100); // Aguarda 100ms
        }

        sleep_ms(100); // Reduz o uso da CPU
    }

    cyw43_arch_deinit(); // Desligar a arquitetura CYW43.
    return 0;
}

/*--------------------------------------------------------------------------------------------------------------------------------------------*/
/*FUNÇÕES*/
/*--------------------------------------------------------------------------------------------------------------------------------------------*/

void gpio_button_bitdog(void) // Inicializar os Pinos GPIO para acionamento dos Botões da BitDogLab
{
    // Configuração do botão A
    gpio_init(BUTTON_A);             // Inicializa o pino do botão A
    gpio_set_dir(BUTTON_A, GPIO_IN); // Configura o pino como entrada
    gpio_pull_up(BUTTON_A);          // Ativa o resistor pull-up interno

    // Configuração do botão B
    gpio_init(BUTTON_B);                                                                       // Inicializa o pino do botão B
    gpio_set_dir(BUTTON_B, GPIO_IN);                                                           // Configura o pino como entrada
    gpio_pull_up(BUTTON_B);                                                                    // Ativa o resistor pull-up interno
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler); // Configura a interrupção para o botão B
}

void gpio_led_bitdog(void) // Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
{
    // Configuração do LED verde
    gpio_init(LED_GREEN_PIN);              // Inicializa o pino do LED verde
    gpio_set_dir(LED_GREEN_PIN, GPIO_OUT); // Configura o pino como saída
    gpio_put(LED_GREEN_PIN, false);        // Desliga o LED verde

    // Configuração do LED azul
    gpio_init(LED_BLUE_PIN);              // Inicializa o pino do LED azul
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT); // Configura o pino como saída
    gpio_put(LED_BLUE_PIN, false);        // Desliga o LED azul

    // Configuração do LED vermelho
    gpio_init(LED_RED_PIN);              // Inicializa o pino do LED vermelho
    gpio_set_dir(LED_RED_PIN, GPIO_OUT); // Configura o pino como saída
    gpio_put(LED_RED_PIN, false);        // Desliga o LED vermelho
}

void start_alarm() // Função para iniciar o alarme
{
    // Evita reentrada se o alarme já estiver ativo
    if (alarm_active)
        return;

    alarm_verification = true; // Ativa a verificação do alarme
    gpio_put(LED_BLUE_PIN, 0);  // Desliga o LED azul
    gpio_put(LED_RED_PIN, 0);   // Desliga o LED vermelho
    gpio_put(LED_GREEN_PIN, 1); // Liga o LED verde
}

void stop_alarm() // Função para parar o alarme
{

    alarm_active = false;       // Desativa o alarme
    alarm_verification = false; // Desativa a verificação do alarme
    buzzer_off(BUZZER1_PIN);    // Desativa o buzzer 1
    buzzer_off(BUZZER2_PIN);    // Desativa o buzzer 2
    gpio_put(LED_GREEN_PIN, 0); // Desliga o LED verde
    gpio_put(LED_BLUE_PIN, 0);  // Desliga o LED azul
    gpio_put(LED_RED_PIN, 1);   // Liga o LED vermelho
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Tratamento do request do usuário - digite aqui
void user_request(char **request)
{

    if (strstr(*request, "GET /alarm_on") != NULL) // Verifica se a requisição contém "/alarm_on"
    {
        start_alarm(); // Inicia o alarme
    }
    else if (strstr(*request, "GET /alarm_off") != NULL) // Verifica se a requisição contém "/alarm_off"
    {
        stop_alarm(); // Para o alarme
    }
};

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // Tratamento de request - Controle dos LEDs
    user_request(&request);

    // Cria a resposta HTML
    char html[1024];

    // Instruções html do webserver
    snprintf(html, sizeof(html), // Formatar uma string e armazená-la em um buffer de caracteres
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "\r\n"
             "<!DOCTYPE html>\n"
             "<html lang=\"pt-br\">\n"
             "<head>\n"
             "<meta charset=\"UTF-8\">\n"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
             "<title>Embarcatech - Sistema de Alarme</title>\n"
             "<style>\n"
             "  body {background-color: rgb(58, 58, 58); font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; text-align: center; padding: 40px; color: white}"

             "  h1 {font-size: 48px;margin-bottom: 20px;}"

             "  .button-container { margin-top: 30px;}"

             "  button {"
             "    width: 250px;"
             "    height: 70px;"
             "    background-color: #007BFF;"
             "    color: white;"
             "    font-size: 24px;"
             "    border: none;"
             "    border-radius: 12px;"
             "    cursor: pointer;"
             "    margin: 10px;"
             "  }"

             "  button:hover { background-color: #0056b3; }"
             "</style>"
             "</head>"
             "<body>"
             "<h1>Sistema de Alarme</h1>\n"
             "<div class=\"button-container\">\n"
             "<form action=\"/alarm_on\" method=\"get\">\n"
             "<button>Ativar Alarme</button>\n"
             "</form>\n"
             "<form action=\"/alarm_off\" method=\"get\">\n"
             "<button>Desativar Alarme</button>\n"
             "</form>\n"
             "</div>\n"
             "</body>\n"
             "</html>\n");

    // Escreve dados para envio (mas não os envia imediatamente).
    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);

    // Envia a mensagem
    tcp_output(tpcb);

    // libera memória alocada dinamicamente
    free(request);

    // libera um buffer de pacote (pbuf) que foi alocado anteriormente
    pbuf_free(p);

    return ERR_OK;
}