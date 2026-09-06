#include "mcp23s17.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

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

static spi_device_handle_t s_spi;
static bool s_ok;
static uint16_t s_iodir = 0xFFFF;
static uint16_t s_gppu = 0xFFFF;
static uint16_t s_olat = 0x0000;

static esp_err_t mcp_write8(uint8_t reg, uint8_t val)
{
    spi_transaction_t t = {0};
    uint8_t tx[3] = { MCP_OPCODE_WRITE, reg, val };

    t.length = 24;
    t.tx_buffer = tx;
    t.rx_buffer = NULL;
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t mcp_read8(uint8_t reg, uint8_t *out)
{
    spi_transaction_t t = {0};
    uint8_t tx[3] = { MCP_OPCODE_READ, reg, 0 };
    uint8_t rx[3] = {0};

    t.length = 24;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
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
    uint8_t iodir = 0;
    uint8_t gppu = 0;
    uint8_t iocon = (uint8_t)(IOCON_HAEN | IOCON_SEQOP);

    s_ok = false;
    gpio_reset_pin(PIN_CS);
    gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS, 1);

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(err));
        return err;
    }

    /* No MCP RST on this board. After ESP OTA the expander keeps leftover
     * BANK/IOCON, so write IOCON at both addresses to force BANK=0. */
    (void)mcp_write8(REG_IOCON_BANK1, iocon);
    (void)mcp_write8(REG_IOCON, iocon);
    (void)mcp_write16(REG_GPINTENA, 0x0000);
    (void)mcp_write16(REG_IPOLA, 0x0000);

    s_iodir = 0xFFFF;
    s_gppu = 0xFFFF;
    s_olat = 0x0000;
    err = flush_dir_olat();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MCP register init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = mcp_read8(REG_IODIRA, &iodir);
    if (err != ESP_OK || iodir != 0xFF) {
        ESP_LOGE(TAG, "MCP probe failed (IODIRA=0x%02X err=%s)", iodir, esp_err_to_name(err));
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }
    err = mcp_read8(REG_GPPUA, &gppu);
    if (err != ESP_OK || gppu != 0xFF) {
        ESP_LOGE(TAG, "MCP pull-up probe failed (GPPUA=0x%02X err=%s)", gppu, esp_err_to_name(err));
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }
    err = mcp_read8(REG_GPPUB, &gppu);
    if (err != ESP_OK || gppu != 0xFF) {
        ESP_LOGW(TAG, "GPPUB readback 0x%02X err=%s (GPA pull-ups ok; Port B may stay LOW)",
                 gppu, esp_err_to_name(err));
    }

    s_ok = true;
    ESP_LOGI(TAG, "MCP23S17 addr 0 ready on HSPI 1MHz (IODIR/GPPUA ok, GPPUB=0x%02X)", gppu);
    return ESP_OK;
}

bool mcp23s17_ok(void)
{
    return s_ok;
}

void mcp23s17_all_input_pullup(void)
{
    s_iodir = 0xFFFF;
    s_gppu = 0xFFFF;
    (void)flush_dir_olat();
}

void mcp23s17_pin_output_low(int port)
{
    uint16_t mask;

    if (port < 0 || port >= MCP23S17_PORT_COUNT) {
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

    if (port < 0 || port >= MCP23S17_PORT_COUNT) {
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
