#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <pwm_z42.h>

// Define o valor do registrador MOD do TPM para configurar o período do PWM
#define TPM_MODULE 1000         // Define a frequência do PWM fpwm = (TPM_CLK / (TPM_MODULE * PS))

// Define o LED usando Device Tree
#define LED0_NODE DT_ALIAS(led0) // Azul
#define LED1_NODE DT_ALIAS(led1) // Verde
#define LED2_NODE DT_ALIAS(led2) // Vermelho

// Configurando um Buffer para os dados recebidos
#define TAMANHO_PACOTE 6
static uint8_t pacote[TAMANHO_PACOTE];
static uint8_t indice_pacote = 0;

// ID do aparelho
#define ID 0x01

#define STACK_SIZE 1024

// Variáveis globais para transferir dados para a thread
static bool piscar_vermelho;
static bool piscar_verde;
static bool piscar_azul;
static int delay_pisca;

// Verifica se o LED está definido no Device Tree
#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#else
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

#if DT_NODE_HAS_STATUS(LED1_NODE, okay)
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
#else
#error "Unsupported board: led1 devicetree alias is not defined"
#endif

#if DT_NODE_HAS_STATUS(LED2_NODE, okay)
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);
#else
#error "Unsupported board: led2 devicetree alias is not defined"
#endif

static const struct device *uart0_dev;

K_SEM_DEFINE(piscarled, 0, 1);

static void controlar_leds(bool vermelho, bool verde, bool azul) {
    gpio_pin_set_dt(&led2, vermelho);
    gpio_pin_set_dt(&led1, verde);
    gpio_pin_set_dt(&led, azul);
}

void piscaLed(void *p1, void *p2, void *p3) {

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while(1){
        k_sem_take(&piscarled, K_FOREVER);

        controlar_leds(false, false, false);
        k_msleep(delay_pisca);

        controlar_leds(piscar_vermelho, piscar_verde, piscar_azul);
        k_msleep(delay_pisca);

        controlar_leds(false, false, false);
        k_msleep(delay_pisca);
    }

}

static uint8_t calcular_checksum(uint8_t *dados) {
    uint8_t check = 0;

    for (int i = 0; i < 5; i++) {
        check ^= dados[i];
    }

    return check;
}

static void executar_acao(uint8_t *dados)
{
    switch (dados[4]) {

    case 0x01: // LIGAR
        gpio_pin_set_dt(&led2, dados[1] > 0); // Vermelho
        gpio_pin_set_dt(&led,  dados[2] > 0); // Azul
        gpio_pin_set_dt(&led1, dados[3] > 0); // Verde
        break;

    case 0x02: // Desligar
        if(dados[1] > 0){
            gpio_pin_set_dt(&led2, 0);
        }
        if(dados[2] > 0){
            gpio_pin_set_dt(&led,  0);
        }
        if(dados[3] > 0){
            gpio_pin_set_dt(&led1, 0); 
        }
        break;

    case 0x03: // Toggle
        if(dados[1] > 0){
            gpio_pin_toggle_dt(&led2);
        }
        if(dados[2] > 0){
            gpio_pin_toggle_dt(&led);
        }        
        if(dados[3] > 0){
            gpio_pin_toggle_dt(&led1);
        }
        break;

    case 0x04: // Pisca 1s
        piscar_vermelho = dados[1] > 0;
        piscar_verde = dados[2] > 0;
        piscar_azul = dados[3] > 0;
        delay_pisca = 1000;
        k_sem_give(&piscarled);
        break;

    case 0x05: // Pisca 2s
        piscar_vermelho = dados[1] > 0;
        piscar_verde = dados[2] > 0;
        piscar_azul = dados[3] > 0;
        delay_pisca = 2000;
        k_sem_give(&piscarled);
        break;

    default:
        printk("Acao desconhecida!\n");
        break;
    }

}

static void uart0_cb(const struct device *dev, void *user_data) {
    uint8_t c;

    if (!uart_irq_update(dev)) {
        return;
    }

    /* Clear errors */
    uart_err_check(dev);

    while (uart_fifo_read(dev, &c, 1) == 1) {

        pacote[indice_pacote] = c;
        indice_pacote++;

        if (indice_pacote == TAMANHO_PACOTE) {

            uint8_t check = calcular_checksum(pacote);

            if (pacote[0] == ID && check == pacote[5]) {
                printk("Pacote valido!\n"); 
                executar_acao(pacote);
            } else {
                printk("Pacote invalido!\n");
            }

        indice_pacote = 0;
       
        }       
   
    }

}

K_THREAD_DEFINE(a_tid, STACK_SIZE, piscaLed,
                NULL, NULL, NULL,
                5, 0, 0);

void main(void) {

    int ret;

    // Verifica se o device está pronto
    if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&led1) || !gpio_is_ready_dt(&led2)) {
        printk("Error: LED device %s is not ready\n", led.port->name);
        return;
    }

    // Configura o pino como saída
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Error %d: failed to configure LED pin\n", ret);
        return;
    }

    ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Error %d: failed to configure LED pin\n", ret);
        return;
    }

    ret = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Error %d: failed to configure LED pin\n", ret);
        return;
    }

    /* Get uart0 */
    uart0_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

    if (!device_is_ready(uart0_dev)) {
        printk("uart0 não está pronta!\n");
        return;
    }

    /* Configure uart0 callback */
    uart_irq_callback_user_data_set(uart0_dev, uart0_cb, NULL);
    uart_irq_rx_enable(uart0_dev);

    // Inicializa o módulo TPM2 com:
    // - base do TPMx
    // - fonte de clock PLL/FLL (TPM_CLK)
    // - valor do registrador MOD
    // - tipo de clock (TPM_CLK)
    // - prescaler de 1 a 128 (PS)
    // - modo de operação EDGE_PWM
    pwm_tpm_Init(TPM2, TPM_PLLFLL, TPM_MODULE, TPM_CLK, PS_128, EDGE_PWM);

    // Inicializa o canal 0 do TPM2 para gerar sinal PWM na porta GPIOB_18
    // - modo TPM_PWM_H (nível alto durante o pulso)
    pwm_tpm_Ch_Init(TPM2, 0, TPM_PWM_H, GPIOB, 18);

    while (1) {
        k_sleep(K_FOREVER);
    }
}
