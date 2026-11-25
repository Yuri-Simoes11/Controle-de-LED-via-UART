#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

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

    case 0x01:
        // LIGAR
        gpio_pin_set_dt(&led2, dados[1] > 0);
        gpio_pin_set_dt(&led,  dados[2] > 0); // Verde
        gpio_pin_set_dt(&led1, dados[3] > 0); // Azul
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

    while (1) {
        k_sleep(K_FOREVER);
    }
}
