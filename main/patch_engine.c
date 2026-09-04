#include "patch_engine.h"
#include "mcp23s17.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lib_json_helper.h"
#include "cJSON.h"

static const char *TAG = "Patch32Prop";

#define PATCH_CONFIG_FILE_PATH "/spiffs/config.json"
#define PATCH_CONFIG_JSON_MAX 2048
#define PATCH_EVENT_QUEUE_LEN 8
#define PATCH_EVENT_JSON_MAX 256
#define PORT_COUNT 16
#define FAN_GPIO GPIO_NUM_23
#define TILE_GPIO GPIO_NUM_18
#define PATCH_TARGET_SET_COUNT 8
#define PATCH_TARGET_LABEL_MAX 16
#define PATCH_TARGET_CHAINS_MAX 48

typedef struct {
    char label[PATCH_TARGET_LABEL_MAX];
    char chains[PATCH_TARGET_CHAINS_MAX];
} patch_target_set_t;

typedef struct {
    int scan_period_ms;
    int debounce_count;
    int settle_ms;
    int heartbeat_interval_ms;
    bool debug;
    bool debug_mqtt;
    /* Display-only Live overlays. The prop does not match or solve against these. */
    patch_target_set_t targets[PATCH_TARGET_SET_COUNT];
} patch_config_t;

typedef struct {
    patch_config_t cfg;
    char chains[80];
    char chains_unique[80];
    char chains_archive[80];
    char pending_chains[80];
    char last_pending[80];
    int chain_debounce;
    bool all_tiles_present;
    int tile_debounce;
    bool fans_on;
    bool spi_ok;
    uint16_t last_gpio;
    uint8_t port_chain[PORT_COUNT];
    char mqtt_last_in[96];
    int64_t mqtt_last_in_ts_ms;
    char mqtt_last_out[160];
    int64_t mqtt_last_out_ts_ms;
    bool wifi_connected;
    char wifi_ssid[33];
    int wifi_rssi;
    bool spiffs_ready;
    char event_queue[PATCH_EVENT_QUEUE_LEN][PATCH_EVENT_JSON_MAX];
    int event_head;
    int event_tail;
    SemaphoreHandle_t lock;
} patch_ctx_t;

static patch_ctx_t s_ctx;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void copy_bounded(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void sanitize_target_label(char *dst, size_t dst_size, const char *src)
{
    size_t o = 0;

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    for (; *src && o + 1 < dst_size; src++) {
        unsigned char c = (unsigned char)*src;
        if (c < 32 || c > 126 || c == '"' || c == '\\') {
            continue;
        }
        dst[o++] = (char)c;
    }
    dst[o] = '\0';
}

static void sanitize_target_chains(char *dst, size_t dst_size, const char *src)
{
    size_t o = 0;

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    for (; *src && o + 1 < dst_size; src++) {
        char c = *src;
        if (c >= 'a' && c <= 'f') {
            c = (char)(c - 32);
        }
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || c == ',') {
            dst[o++] = c;
        }
    }
    dst[o] = '\0';
}

static void set_default_target_sets(patch_config_t *cfg)
{
    static const char k_labels[] = "ABCDEFGH";
    int i;

    if (!cfg) {
        return;
    }
    for (i = 0; i < PATCH_TARGET_SET_COUNT; ++i) {
        cfg->targets[i].label[0] = k_labels[i];
        cfg->targets[i].label[1] = '\0';
        cfg->targets[i].chains[0] = '\0';
    }
    copy_bounded(cfg->targets[0].chains, sizeof(cfg->targets[0].chains), "09,28,3E,4C");
    copy_bounded(cfg->targets[1].chains, sizeof(cfg->targets[1].chains), "1D,23,56,7A");
}

static int clamp_int(int value, int min_v, int max_v)
{
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

static bool patch_lock(void)
{
    if (!s_ctx.lock) {
        return true;
    }
    return xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(2000)) == pdTRUE;
}

static void patch_unlock(void)
{
    if (s_ctx.lock) {
        xSemaphoreGive(s_ctx.lock);
    }
}

static void set_default_config(patch_config_t *cfg)
{
    cfg->scan_period_ms = 110;
    cfg->debounce_count = 3;
    cfg->settle_ms = 5;
    cfg->heartbeat_interval_ms = 10000;
    cfg->debug = true;
    cfg->debug_mqtt = false;
    set_default_target_sets(cfg);
}

static char hexdig(unsigned i)
{
    if (i < 10) {
        return (char)('0' + i);
    }
    if (i < 16) {
        return (char)('A' + (i - 10));
    }
    return '?';
}

static int uf_find(int *parent, int x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

static void uf_union(int *parent, int a, int b)
{
    a = uf_find(parent, a);
    b = uf_find(parent, b);
    if (a != b) {
        parent[b] = a;
    }
}

static void format_unique_chains(const uint16_t adj[PORT_COUNT], char *out, size_t out_size)
{
    int parent[PORT_COUNT];
    int i;
    int j;
    int groups[PORT_COUNT][PORT_COUNT];
    int gcount[PORT_COUNT];
    int n_groups = 0;
    size_t pos = 0;

    out[0] = '\0';
    for (i = 0; i < PORT_COUNT; ++i) {
        parent[i] = i;
        gcount[i] = 0;
    }
    for (i = 0; i < PORT_COUNT; ++i) {
        for (j = i + 1; j < PORT_COUNT; ++j) {
            if (adj[i] & (1u << j)) {
                uf_union(parent, i, j);
            }
        }
    }
    for (i = 0; i < PORT_COUNT; ++i) {
        int root = uf_find(parent, i);
        int g;
        int member_count = 0;
        if (i != root) {
            continue;
        }
        for (j = 0; j < PORT_COUNT; ++j) {
            if (uf_find(parent, j) == root) {
                member_count++;
            }
        }
        if (member_count < 2) {
            continue;
        }
        g = n_groups++;
        gcount[g] = 0;
        for (j = 0; j < PORT_COUNT; ++j) {
            if (uf_find(parent, j) == root) {
                groups[g][gcount[g]++] = j;
            }
        }
    }
    for (i = 0; i < n_groups; ++i) {
        if (i > 0 && pos + 1 < out_size) {
            out[pos++] = ',';
        }
        for (j = 0; j < gcount[i]; ++j) {
            if (pos + 1 < out_size) {
                out[pos++] = hexdig((unsigned)groups[i][j]);
            }
        }
    }
    if (pos < out_size) {
        out[pos] = '\0';
    } else if (out_size > 0) {
        out[out_size - 1] = '\0';
    }
}

static void format_archive_chains(const uint16_t adj[PORT_COUNT], char *out, size_t out_size)
{
    size_t pos = 0;
    int i;
    int j;

    out[0] = '\0';
    for (i = 0; i < PORT_COUNT - 1; ++i) {
        bool no_chains = true;
        for (j = i + 1; j < PORT_COUNT; ++j) {
            if ((adj[i] & (1u << j)) == 0) {
                continue;
            }
            if (pos > 0 && pos + 1 < out_size) {
                out[pos++] = ',';
            }
            if (no_chains && pos + 1 < out_size) {
                out[pos++] = hexdig((unsigned)i);
            }
            if (pos + 1 < out_size) {
                out[pos++] = hexdig((unsigned)j);
            }
            no_chains = false;
        }
    }
    if (pos < out_size) {
        out[pos] = '\0';
    } else if (out_size > 0) {
        out[out_size - 1] = '\0';
    }
}

static void assign_port_chains(void)
{
    int i;
    int chain_id = 0;

    memset(s_ctx.port_chain, 0, sizeof(s_ctx.port_chain));
    /* Parse unique string groups: comma-separated hex runs. */
    {
        const char *p = s_ctx.chains_unique;
        while (*p) {
            const char *start = p;
            chain_id++;
            while (*p && *p != ',') {
                int v;
                char c = *p++;
                if (c >= '0' && c <= '9') {
                    v = c - '0';
                } else if (c >= 'A' && c <= 'F') {
                    v = 10 + (c - 'A');
                } else if (c >= 'a' && c <= 'f') {
                    v = 10 + (c - 'a');
                } else {
                    continue;
                }
                if (v >= 0 && v < PORT_COUNT) {
                    s_ctx.port_chain[v] = (uint8_t)chain_id;
                }
            }
            (void)start;
            if (*p == ',') {
                p++;
            }
        }
    }
    (void)i;
}

static void queue_event_unlocked(const char *json)
{
    int next = (s_ctx.event_head + 1) % PATCH_EVENT_QUEUE_LEN;
    if (next == s_ctx.event_tail) {
        s_ctx.event_tail = (s_ctx.event_tail + 1) % PATCH_EVENT_QUEUE_LEN;
    }
    copy_bounded(s_ctx.event_queue[s_ctx.event_head], PATCH_EVENT_JSON_MAX, json);
    s_ctx.event_head = next;
}

static void record_mqtt_out_unlocked(const char *payload)
{
    copy_bounded(s_ctx.mqtt_last_out, sizeof(s_ctx.mqtt_last_out), payload);
    s_ctx.mqtt_last_out_ts_ms = now_ms();
}

static void record_mqtt_in_unlocked(const char *payload)
{
    copy_bounded(s_ctx.mqtt_last_in, sizeof(s_ctx.mqtt_last_in), payload);
    s_ctx.mqtt_last_in_ts_ms = now_ms();
}

static void publish_chains_unlocked(void)
{
    char payload[PATCH_EVENT_JSON_MAX];
    snprintf(payload, sizeof(payload), "{\"Chains\":\"%s\"}", s_ctx.chains);
    queue_event_unlocked(payload);
    record_mqtt_out_unlocked(payload);
}

static void publish_tiles_unlocked(void)
{
    char payload[96];
    snprintf(payload, sizeof(payload), "{\"AllTilesPresent\":%s}", s_ctx.all_tiles_present ? "true" : "false");
    queue_event_unlocked(payload);
    record_mqtt_out_unlocked(payload);
}

static void set_fans_unlocked(bool on)
{
    s_ctx.fans_on = on;
    gpio_set_level(FAN_GPIO, on ? 1 : 0);
}

static bool read_tiles_present(void)
{
    /* INPUT_PULLUP: LOW = all tiles present. */
    return gpio_get_level(TILE_GPIO) == 0;
}

static void scan_once_unlocked(void)
{
    uint16_t adj[PORT_COUNT];
    int i;
    int j;
    int settle = s_ctx.cfg.settle_ms;

    memset(adj, 0, sizeof(adj));
    if (settle < 1) {
        settle = 1;
    }

    mcp23s17_all_input_pullup();
    for (i = 0; i < PORT_COUNT - 1; ++i) {
        uint16_t gpio;
        mcp23s17_pin_output_low(i);
        vTaskDelay(pdMS_TO_TICKS(settle));
        gpio = mcp23s17_read_gpio();
        for (j = i + 1; j < PORT_COUNT; ++j) {
            if ((gpio & (1u << j)) == 0) {
                adj[i] |= (uint16_t)(1u << j);
                adj[j] |= (uint16_t)(1u << i);
            }
        }
        mcp23s17_pin_input_pullup(i);
    }
    s_ctx.last_gpio = mcp23s17_read_gpio();
    s_ctx.spi_ok = mcp23s17_ok();
    format_unique_chains(adj, s_ctx.pending_chains, sizeof(s_ctx.pending_chains));
    format_archive_chains(adj, s_ctx.chains_archive, sizeof(s_ctx.chains_archive));
}

static void apply_debounced_unlocked(void)
{
    bool tiles_now = read_tiles_present();

    if (strcmp(s_ctx.pending_chains, s_ctx.chains) != 0) {
        if (strcmp(s_ctx.pending_chains, s_ctx.last_pending) == 0) {
            s_ctx.chain_debounce++;
            if (s_ctx.chain_debounce >= s_ctx.cfg.debounce_count) {
                copy_bounded(s_ctx.chains, sizeof(s_ctx.chains), s_ctx.pending_chains);
                copy_bounded(s_ctx.chains_unique, sizeof(s_ctx.chains_unique), s_ctx.pending_chains);
                assign_port_chains();
                s_ctx.chain_debounce = 0;
                ESP_LOGI(TAG, "New chains = %s", s_ctx.chains);
                publish_chains_unlocked();
            }
        } else {
            copy_bounded(s_ctx.last_pending, sizeof(s_ctx.last_pending), s_ctx.pending_chains);
            s_ctx.chain_debounce = 1;
        }
    } else {
        s_ctx.chain_debounce = 0;
        copy_bounded(s_ctx.last_pending, sizeof(s_ctx.last_pending), s_ctx.pending_chains);
    }

    if (tiles_now == s_ctx.all_tiles_present) {
        s_ctx.tile_debounce = 0;
    } else {
        s_ctx.tile_debounce++;
        if (s_ctx.tile_debounce >= s_ctx.cfg.debounce_count) {
            s_ctx.all_tiles_present = tiles_now;
            s_ctx.tile_debounce = 0;
            ESP_LOGI(TAG, "All tiles present is %d", (int)s_ctx.all_tiles_present);
            publish_tiles_unlocked();
        }
    }
}

static void patch_loop_task(void *arg)
{
    (void)arg;
    while (1) {
        int delay_ms;
        if (patch_lock()) {
            scan_once_unlocked();
            apply_debounced_unlocked();
            delay_ms = s_ctx.cfg.scan_period_ms;
            patch_unlock();
        } else {
            delay_ms = 110;
        }
        if (delay_ms < 20) {
            delay_ms = 20;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 6,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        return err;
    }
    s_ctx.spiffs_ready = true;
    return ESP_OK;
}

static void apply_config_fields_unlocked(const char *json)
{
    int i_val;
    bool b_val;

    if (!json) {
        return;
    }
    if (lib_json_extract_int(json, "scanPeriodMs", &i_val)) {
        s_ctx.cfg.scan_period_ms = clamp_int(i_val, 20, 2000);
    }
    if (lib_json_extract_int(json, "debounceCount", &i_val)) {
        s_ctx.cfg.debounce_count = clamp_int(i_val, 1, 20);
    }
    if (lib_json_extract_int(json, "settleMs", &i_val)) {
        s_ctx.cfg.settle_ms = clamp_int(i_val, 1, 40);
    }
    if (lib_json_extract_int(json, "heartbeatInterval", &i_val)) {
        s_ctx.cfg.heartbeat_interval_ms = clamp_int(i_val, 1000, 120000);
    }
    if (lib_json_extract_bool(json, "debug", &b_val)) {
        s_ctx.cfg.debug = b_val;
    }
    if (lib_json_extract_bool(json, "debugMqtt", &b_val)) {
        s_ctx.cfg.debug_mqtt = b_val;
    }
    {
        cJSON *root = lib_json_parse(json);
        if (root) {
            cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "targetSets");
            if (cJSON_IsArray(arr)) {
                int n = cJSON_GetArraySize(arr);
                int i;
                for (i = 0; i < PATCH_TARGET_SET_COUNT; ++i) {
                    cJSON *item = (i < n) ? cJSON_GetArrayItem(arr, i) : NULL;
                    char label[PATCH_TARGET_LABEL_MAX] = "";
                    char chains[PATCH_TARGET_CHAINS_MAX] = "";
                    if (cJSON_IsObject(item)) {
                        (void)lib_json_get_string(item, "label", label, sizeof(label));
                        (void)lib_json_get_string(item, "chains", chains, sizeof(chains));
                    }
                    sanitize_target_label(s_ctx.cfg.targets[i].label,
                                          sizeof(s_ctx.cfg.targets[i].label),
                                          label);
                    if (s_ctx.cfg.targets[i].label[0] == '\0') {
                        s_ctx.cfg.targets[i].label[0] = (char)('A' + i);
                        s_ctx.cfg.targets[i].label[1] = '\0';
                    }
                    sanitize_target_chains(s_ctx.cfg.targets[i].chains,
                                           sizeof(s_ctx.cfg.targets[i].chains),
                                           chains);
                }
            }
            cJSON_Delete(root);
        }
    }
}

static int config_json_append(char *out, size_t out_size, int pos, const char *fmt, ...)
{
    va_list args;
    int n;

    if (!out || out_size == 0 || pos < 0 || (size_t)pos >= out_size) {
        return (pos < 0) ? 0 : pos;
    }
    va_start(args, fmt);
    n = vsnprintf(out + pos, out_size - (size_t)pos, fmt, args);
    va_end(args);
    if (n < 0) {
        out[out_size - 1] = '\0';
        return (int)(out_size - 1);
    }
    if ((size_t)n >= out_size - (size_t)pos) {
        return (int)(out_size - 1);
    }
    return pos + n;
}

static void fill_config_json(char *out, size_t out_size, const patch_config_t *cfg)
{
    int pos = 0;
    int i;

    if (!out || out_size == 0 || !cfg) {
        return;
    }
    pos = config_json_append(out,
                             out_size,
                             pos,
                             "{"
                             "\"scanPeriodMs\":%d,"
                             "\"debounceCount\":%d,"
                             "\"settleMs\":%d,"
                             "\"heartbeatInterval\":%d,"
                             "\"debug\":%s,"
                             "\"debugMqtt\":%s,"
                             "\"targetSets\":[",
                             cfg->scan_period_ms,
                             cfg->debounce_count,
                             cfg->settle_ms,
                             cfg->heartbeat_interval_ms,
                             cfg->debug ? "true" : "false",
                             cfg->debug_mqtt ? "true" : "false");
    for (i = 0; i < PATCH_TARGET_SET_COUNT; ++i) {
        char elab[48];
        char ech[96];
        lib_json_escape_string(cfg->targets[i].label, elab, sizeof(elab));
        lib_json_escape_string(cfg->targets[i].chains, ech, sizeof(ech));
        pos = config_json_append(out,
                                 out_size,
                                 pos,
                                 "%s{\"label\":\"%s\",\"chains\":\"%s\"}",
                                 i ? "," : "",
                                 elab,
                                 ech);
    }
    (void)config_json_append(out, out_size, pos, "]}");
}

static esp_err_t save_config_file_unlocked(void)
{
    FILE *f;
    char json[PATCH_CONFIG_JSON_MAX];

    if (!s_ctx.spiffs_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    fill_config_json(json, sizeof(json), &s_ctx.cfg);
    f = fopen(PATCH_CONFIG_FILE_PATH, "w");
    if (!f) {
        return ESP_FAIL;
    }
    fputs(json, f);
    fclose(f);
    return ESP_OK;
}

static void load_config_file_if_present(void)
{
    FILE *f;
    char buf[PATCH_CONFIG_JSON_MAX];
    size_t n;

    if (!s_ctx.spiffs_ready) {
        return;
    }
    f = fopen(PATCH_CONFIG_FILE_PATH, "r");
    if (!f) {
        return;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    apply_config_fields_unlocked(buf);
}

static int state_json_append(char *out, size_t out_size, int pos, const char *fmt, ...)
{
    va_list args;
    int n;

    if (!out || out_size == 0 || pos < 0 || (size_t)pos >= out_size) {
        return (pos < 0) ? 0 : pos;
    }
    va_start(args, fmt);
    n = vsnprintf(out + pos, out_size - (size_t)pos, fmt, args);
    va_end(args);
    if (n < 0) {
        out[out_size - 1] = '\0';
        return (int)(out_size - 1);
    }
    if ((size_t)n >= out_size - (size_t)pos) {
        return (int)(out_size - 1);
    }
    return pos + n;
}

esp_err_t patch_engine_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.lock = xSemaphoreCreateMutex();
    if (!s_ctx.lock) {
        return ESP_ERR_NO_MEM;
    }
    set_default_config(&s_ctx.cfg);
    (void)init_spiffs();
    load_config_file_if_present();

    gpio_reset_pin(FAN_GPIO);
    gpio_set_direction(FAN_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(FAN_GPIO, 0);

    gpio_config_t tile_cfg = {
        .pin_bit_mask = 1ULL << TILE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&tile_cfg);

    if (mcp23s17_init() != ESP_OK) {
        ESP_LOGE(TAG, "MCP23S17 init failed — scan will report empty chains");
        s_ctx.spi_ok = false;
    } else {
        s_ctx.spi_ok = true;
    }

    s_ctx.all_tiles_present = read_tiles_present();

    if (xTaskCreate(patch_loop_task, "patch_loop", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Patch engine initialized");
    return ESP_OK;
}

void patch_engine_get_state_json(char *out, size_t out_size)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char esc_chains[96];
    char esc_unique[96];
    char esc_archive[96];
    char esc_mqtt_in[192];
    char esc_mqtt_out[192];
    char esc_ssid[72];
    int i;
    int pos = 0;
    bool tile_raw_high;

    if (!out || out_size < 32) {
        return;
    }
    if (!patch_lock()) {
        out[0] = '\0';
        return;
    }

    lib_json_escape_string(s_ctx.chains, esc_chains, sizeof(esc_chains));
    lib_json_escape_string(s_ctx.chains_unique, esc_unique, sizeof(esc_unique));
    lib_json_escape_string(s_ctx.chains_archive, esc_archive, sizeof(esc_archive));
    lib_json_escape_string(s_ctx.mqtt_last_in, esc_mqtt_in, sizeof(esc_mqtt_in));
    lib_json_escape_string(s_ctx.mqtt_last_out, esc_mqtt_out, sizeof(esc_mqtt_out));
    lib_json_escape_string(s_ctx.wifi_ssid, esc_ssid, sizeof(esc_ssid));
    tile_raw_high = gpio_get_level(TILE_GPIO) != 0;

    pos = state_json_append(out,
                            out_size,
                            pos,
                            "{"
                            "\"ts\":%lld,"
                            "\"id\":\"Patch32Prop\","
                            "\"status\":\"online\","
                            "\"version\":\"%s\","
                            "\"buildId\":\"%s\","
                            "\"chains\":\"%s\","
                            "\"chainsUnique\":\"%s\","
                            "\"chainsArchive\":\"%s\","
                            "\"allTilesPresent\":%s,"
                            "\"tileRawHigh\":%s,"
                            "\"fansOn\":%s,"
                            "\"spiOk\":%s,"
                            "\"ports\":[",
                            (long long)now_ms(),
                            app ? app->version : "0.0.0",
                            app ? app->version : "0.0.0",
                            esc_chains,
                            esc_unique,
                            esc_archive,
                            s_ctx.all_tiles_present ? "true" : "false",
                            tile_raw_high ? "true" : "false",
                            s_ctx.fans_on ? "true" : "false",
                            s_ctx.spi_ok ? "true" : "false");

    for (i = 0; i < PORT_COUNT; ++i) {
        bool connected = s_ctx.port_chain[i] != 0;
        int level = (s_ctx.last_gpio & (1u << i)) ? 1 : 0;
        char chain_hex[8] = "";
        if (connected) {
            snprintf(chain_hex, sizeof(chain_hex), "%d", (int)s_ctx.port_chain[i]);
        }
        pos = state_json_append(out,
                                out_size,
                                pos,
                                "%s{\"id\":%d,\"hex\":\"%c\",\"connected\":%s,\"level\":%d,\"driven\":false,\"chain\":\"%s\"}",
                                i > 0 ? "," : "",
                                i,
                                hexdig((unsigned)i),
                                connected ? "true" : "false",
                                level,
                                chain_hex);
    }

    pos = state_json_append(out,
                            out_size,
                            pos,
                            "],"
                            "\"mqtt\":{"
                            "\"lastIn\":\"%s\","
                            "\"lastInTs\":%lld,"
                            "\"lastOut\":\"%s\","
                            "\"lastOutTs\":%lld"
                            "},"
                            "\"wifiConnected\":%s,"
                            "\"wifiSsid\":\"%s\","
                            "\"wifiRssi\":%d"
                            "}",
                            esc_mqtt_in,
                            (long long)s_ctx.mqtt_last_in_ts_ms,
                            esc_mqtt_out,
                            (long long)s_ctx.mqtt_last_out_ts_ms,
                            s_ctx.wifi_connected ? "true" : "false",
                            esc_ssid,
                            s_ctx.wifi_rssi);
    (void)pos;
    patch_unlock();
}

void patch_engine_get_config_json(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    if (!patch_lock()) {
        out[0] = '\0';
        return;
    }
    fill_config_json(out, out_size, &s_ctx.cfg);
    patch_unlock();
}

void patch_engine_get_default_config_json(char *out, size_t out_size)
{
    patch_config_t defaults;
    set_default_config(&defaults);
    if (!out || out_size == 0) {
        return;
    }
    fill_config_json(out, out_size, &defaults);
}

static esp_err_t handle_command_unlocked(const char *command, char *response, size_t response_size)
{
    char mqtt_in[96];

    if (!command || command[0] == '\0') {
        snprintf(response, response_size, "{\"ok\":false,\"error\":\"missing_command\"}");
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(mqtt_in, sizeof(mqtt_in), "Command:%s", command);
    record_mqtt_in_unlocked(mqtt_in);

    if (strcmp(command, "fansOn") == 0) {
        set_fans_unlocked(true);
        snprintf(response, response_size, "{\"ok\":true,\"Command\":\"fansOn\"}");
        return ESP_OK;
    }
    if (strcmp(command, "fansOff") == 0) {
        set_fans_unlocked(false);
        snprintf(response, response_size, "{\"ok\":true,\"Command\":\"fansOff\"}");
        return ESP_OK;
    }
    if (strcmp(command, "reportState") == 0) {
        /* Same invert as the scan loop — do not publish raw GPIO. */
        s_ctx.all_tiles_present = read_tiles_present();
        publish_chains_unlocked();
        publish_tiles_unlocked();
        snprintf(response, response_size, "{\"ok\":true,\"Command\":\"reportState\"}");
        return ESP_OK;
    }

    snprintf(response, response_size, "{\"ok\":false,\"error\":\"invalid_command\"}");
    return ESP_ERR_INVALID_ARG;
}

esp_err_t patch_engine_handle_command_json(const char *json, char *response, size_t response_size)
{
    char command[32] = "";
    cJSON *root = NULL;

    if (!json || !response || response_size < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!patch_lock()) {
        snprintf(response, response_size, "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }

    root = lib_json_parse(json);
    if (root) {
        (void)lib_json_get_string(root, "command", command, sizeof(command));
        if (command[0] == '\0') {
            (void)lib_json_get_string(root, "Command", command, sizeof(command));
        }
        cJSON_Delete(root);
    }

    {
        esp_err_t err = handle_command_unlocked(command, response, response_size);
        patch_unlock();
        return err;
    }
}

esp_err_t patch_engine_apply_config_json(const char *json, bool persist, char *response, size_t response_size)
{
    esp_err_t err = ESP_OK;

    if (!json || !response || response_size < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!patch_lock()) {
        snprintf(response, response_size, "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }
    apply_config_fields_unlocked(json);
    if (persist) {
        err = save_config_file_unlocked();
    }
    snprintf(response,
             response_size,
             "{\"ok\":%s,\"persisted\":%s,\"spiffs\":%s}",
             err == ESP_OK ? "true" : "false",
             persist ? "true" : "false",
             s_ctx.spiffs_ready ? "true" : "false");
    patch_unlock();
    return ESP_OK;
}

esp_err_t patch_engine_restore_defaults(bool persist, char *response, size_t response_size)
{
    esp_err_t err = ESP_OK;

    if (!response || response_size < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!patch_lock()) {
        snprintf(response, response_size, "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }
    set_default_config(&s_ctx.cfg);
    if (persist) {
        err = save_config_file_unlocked();
    }
    snprintf(response,
             response_size,
             "{\"ok\":%s,\"restored\":true,\"persisted\":%s,\"spiffs\":%s}",
             err == ESP_OK ? "true" : "false",
             persist ? "true" : "false",
             s_ctx.spiffs_ready ? "true" : "false");
    patch_unlock();
    return ESP_OK;
}

bool patch_engine_pop_event_json(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }
    if (!patch_lock()) {
        return false;
    }
    if (s_ctx.event_tail == s_ctx.event_head) {
        patch_unlock();
        return false;
    }
    copy_bounded(out, out_size, s_ctx.event_queue[s_ctx.event_tail]);
    s_ctx.event_tail = (s_ctx.event_tail + 1) % PATCH_EVENT_QUEUE_LEN;
    patch_unlock();
    return true;
}

bool patch_engine_pop_warning_message(char *out, size_t out_size)
{
    (void)out;
    (void)out_size;
    return false;
}

void patch_engine_notify_wifi_connected(int rssi)
{
    if (!patch_lock()) {
        return;
    }
    s_ctx.wifi_connected = true;
    s_ctx.wifi_rssi = rssi;
    patch_unlock();
}

void patch_engine_notify_wifi_disconnected(void)
{
    if (!patch_lock()) {
        return;
    }
    s_ctx.wifi_connected = false;
    s_ctx.wifi_rssi = 0;
    s_ctx.wifi_ssid[0] = '\0';
    patch_unlock();
}

void patch_engine_set_wifi_ssid(const char *ssid)
{
    if (!patch_lock()) {
        return;
    }
    copy_bounded(s_ctx.wifi_ssid, sizeof(s_ctx.wifi_ssid), ssid ? ssid : "");
    patch_unlock();
}

patch_led_hint_t patch_engine_get_led_hint(void)
{
    return PATCH_LED_HINT_OFF;
}

void patch_engine_get_battery_snapshot(patch_battery_snapshot_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    copy_bounded(out->battery_profile, sizeof(out->battery_profile), "none");
}
