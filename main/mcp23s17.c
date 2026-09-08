#include "mcp23s17.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp23s17";

#define PIN_MISO 12
#define PIN_MOSI 13
#define PIN_SCK  14
#define PIN_CS   15

#define MCP_OPCODE_WRITE 0x40
#define MCP_OPCODE_READ  0x41

#define REG_IODIRA     0x00
#define REG_IODIRB     0x01
#define REG_IPOLA      0x02
#define REG_GPINTENA   0x04
#define REG_IOCON_BANK1 0x05 /* BANK=1 IOCON; BANK=0 GPINTENB */
#define REG_GPPUA      0x0C
#define REG_GPPUB      0x0D
#define REG_GPIOA      0x12
#define REG_GPIOB      0x13
#define REG_OLATA      0x14
#define REG_OLATB      0x15
#define REG_IOCON      0x0A

#define IOCON_HAEN  (1u << 3)
#define IOCON_SEQOP (1u << 5)

/* 24 bits at 1 MHz is ~24 us; 80 ms bounds a wedged controller. */
#define MCP_SPI_WAIT pdMS_TO_TICKS(80)

static spi_device_handle_t s_spi;
static bool s_ok;
static char s_fault[128];
static char s_spi_error[48];
static uint16_t s_iodir = 0xFFFF;
static uint16_t s_gppu = 0xFFFF;
static uint16_t s_olat = 0x0000;

static void refresh_fault(void)
{
    if (s_spi_error[0]) {
        snprintf(s_fault, sizeof(s_fault),
                 "Hardware: %s — MCP23S17 GPIO expander unavailable.", s_spi_error);
        return;
    }
    if (s_ok) {
        s_fault[0] = '\0';
        return;
    }
    snprintf(s_fault, sizeof(s_fault),
             "Hardware: MCP23S17 GPIO expander not found. Patch panel I/O disabled.");
}

static esp_err_t mcp_xfer(const uint8_t *tx, uint8_t *rx)
{
    spi_transaction_t t = {0};
    if (!s_spi) {
        return ESP_ERR_INVALID_STATE;
    }
    t.length = 24;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    /* IDF 6 rejects polling_start with a finite timeout (that is why the first
     * 0.13 probe "missed" a present chip) and polling_transmit cannot time
     * out. Interrupt transactions give a bounded wait. */
    spi_transaction_t *done = NULL;
    esp_err_t err = spi_device_queue_trans(s_spi, &t, MCP_SPI_WAIT);
    if (err != ESP_OK) {
        return err;
    }
    err = spi_device_get_trans_result(s_spi, &done, MCP_SPI_WAIT);
    if (err != ESP_OK) {
        return err;
    }
    return (done == &t) ? ESP_OK : ESP_FAIL;
}

static esp_err_t mcp_write8(uint8_t reg, uint8_t val)
{
    uint8_t tx[3] = { MCP_OPCODE_WRITE, reg, val };
    return mcp_xfer(tx, NULL);
}

static esp_err_t mcp_read8(uint8_t reg, uint8_t *out)
{
    uint8_t tx[3] = { MCP_OPCODE_READ, reg, 0 };
    uint8_t rx[3] = {0};
    esp_err_t err = mcp_xfer(tx, rx);
    if (err == ESP_OK && out) {
        *out = rx[2];
    }
    return err;
}

static esp_err_t mcp_write16(uint8_t reg_a, uint16_t val)
{
    esp_err_t err = mcp_write8(reg_a, (uint8_t)(val & 0xFF));
    if (err != ESP_OK) {
        return err;
    }
    return mcp_write8((uint8_t)(reg_a + 1), (uint8_t)(val >> 8));
}

static esp_err_t flush_dir_olat(void)
{
    esp_err_t err = mcp_write16(REG_IODIRA, s_iodir);
    if (err != ESP_OK) {
        return err;
    }
    err = mcp_write16(REG_OLATA, s_olat);
    if (err != ESP_OK) {
        return err;
    }
    return mcp_write16(REG_GPPUA, s_gppu);
}

esp_err_t mcp23s17_init(void)
{
    spi_bus_config_t bus = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .flags = 0,
    };
    s_ok = false;
    gpio_reset_pin(PIN_CS);
    gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS, 1);

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        snprintf(s_spi_error, sizeof(s_spi_error), "SPI bus init failed (%s)",
                 esp_err_to_name(err));
        refresh_fault();
        return ESP_OK;
    }

    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        snprintf(s_spi_error, sizeof(s_spi_error), "SPI device add failed (%s)",
                 esp_err_to_name(err));
        s_spi = NULL;
        refresh_fault();
        return ESP_OK;
    }

    (void)mcp23s17_probe();
    if (s_ok) {
        ESP_LOGI(TAG, "MCP23S17 addr 0 ready on HSPI 1MHz");
    } else {
        ESP_LOGW(TAG, "%s", s_fault);
    }
    return ESP_OK;
}

bool mcp23s17_probe(void)
{
    uint8_t a = 0xFF;
    uint8_t b = 0xFF;
    uint8_t iocon = (uint8_t)(IOCON_HAEN | IOCON_SEQOP);

    if (!s_spi) {
        s_ok = false;
        refresh_fault();
        return false;
    }

    /* No MCP RST on this board. After ESP OTA the expander keeps leftover
     * BANK/IOCON, so write IOCON at both addresses to force BANK=0. */
    (void)mcp_write8(REG_IOCON_BANK1, iocon);
    (void)mcp_write8(REG_IOCON, iocon);
    (void)mcp_write16(REG_GPINTENA, 0x0000);
    (void)mcp_write16(REG_IPOLA, 0x0000);

    /* IODIRA is 0x00 in both BANK maps, so leftover IOCON cannot hide the chip. */
    if (mcp_write8(REG_IODIRA, 0xA5) != ESP_OK ||
        mcp_read8(REG_IODIRA, &a) != ESP_OK || a != 0xA5 ||
        mcp_write8(REG_IODIRA, 0x5A) != ESP_OK ||
        mcp_read8(REG_IODIRA, &b) != ESP_OK || b != 0x5A) {
        s_ok = false;
        refresh_fault();
        return false;
    }
    s_iodir = 0xFFFF;
    s_gppu = 0xFFFF;
    s_olat = 0x0000;
    (void)flush_dir_olat();
    s_ok = true;
    refresh_fault();
    return true;
}

bool mcp23s17_ok(void)
{
    return s_ok;
}

void mcp23s17_get_fault(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    if (s_ok) {
        out[0] = '\0';
        return;
    }
    strncpy(out, s_fault, out_size - 1);
    out[out_size - 1] = '\0';
}

void mcp23s17_all_input_pullup(void)
{
    if (!s_ok) {
        return;
    }
    s_iodir = 0xFFFF;
    s_gppu = 0xFFFF;
    (void)flush_dir_olat();
}

void mcp23s17_pin_output_low(int port)
{
    uint16_t mask;

    if (!s_ok || port < 0 || port >= MCP23S17_PORT_COUNT) {
        return;
    }
    mask = (uint16_t)(1u << port);
    s_iodir &= (uint16_t)~mask;
    s_olat &= (uint16_t)~mask;
    /* Keep GPPU on the remaining inputs; OTA leftovers used to drop them. */
    (void)flush_dir_olat();
}

void mcp23s17_pin_input_pullup(int port)
{
    uint16_t mask;

    if (!s_ok || port < 0 || port >= MCP23S17_PORT_COUNT) {
        return;
    }
    mask = (uint16_t)(1u << port);
    s_iodir |= mask;
    s_gppu |= mask;
    (void)flush_dir_olat();
}

uint16_t mcp23s17_read_gpio(void)
{
    uint8_t a = 0xFF;
    uint8_t b = 0xFF;

    if (!s_ok) {
        return 0xFFFF;
    }
    if (mcp_read8(REG_GPIOA, &a) != ESP_OK) {
        s_ok = false;
        return 0xFFFF;
    }
    if (mcp_read8(REG_GPIOB, &b) != ESP_OK) {
        s_ok = false;
        return 0xFFFF;
    }
    return (uint16_t)a | ((uint16_t)b << 8);
}

int mcp23s17_digital_read(int port)
{
    uint16_t gpio;

    if (port < 0 || port >= MCP23S17_PORT_COUNT) {
        return 1;
    }
    gpio = mcp23s17_read_gpio();
    return (gpio & (1u << port)) ? 1 : 0;
}
