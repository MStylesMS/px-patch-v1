#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MCP23S17_PORT_COUNT 16

esp_err_t mcp23s17_init(void);
bool mcp23s17_ok(void);

void mcp23s17_all_input_pullup(void);
void mcp23s17_pin_output_low(int port);
void mcp23s17_pin_input_pullup(int port);
int mcp23s17_digital_read(int port);
uint16_t mcp23s17_read_gpio(void);
