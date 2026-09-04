#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    PATCH_LED_HINT_OFF = 0,
} patch_led_hint_t;

typedef struct {
    int wire_count;
    int battery_adc_raw;
    int battery_adc_at_0v;
    int battery_adc_at_15v;
    char battery_profile[24];
} patch_battery_snapshot_t;

esp_err_t patch_engine_init(void);

#define PATCH_STATE_JSON_MAX 4096

void patch_engine_get_state_json(char *out, size_t out_size);
void patch_engine_get_config_json(char *out, size_t out_size);
void patch_engine_get_default_config_json(char *out, size_t out_size);

esp_err_t patch_engine_handle_command_json(const char *json, char *response, size_t response_size);
esp_err_t patch_engine_apply_config_json(const char *json, bool persist, char *response, size_t response_size);
esp_err_t patch_engine_restore_defaults(bool persist, char *response, size_t response_size);

bool patch_engine_pop_event_json(char *out, size_t out_size);
bool patch_engine_pop_warning_message(char *out, size_t out_size);

void patch_engine_notify_wifi_connected(int rssi);
void patch_engine_notify_wifi_disconnected(void);
void patch_engine_set_wifi_ssid(const char *ssid);

patch_led_hint_t patch_engine_get_led_hint(void);
void patch_engine_get_battery_snapshot(patch_battery_snapshot_t *out);
