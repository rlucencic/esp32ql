#include "esp_log.h"
#include "nvs.h"
#include "rom_store.h"
#include "settings.h"

#define NAMESPACE   "ql"

static const char *TAG = "settings";

settings_t settings;

void settings_load(void)
{
    settings.rom = ROM_JS;
    settings.memory = 0;

    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t u8;
    if (nvs_get_u8(h, "rom", &u8) == ESP_OK && u8 < ROM_COUNT) {
        settings.rom = u8;
    }
    if (nvs_get_u8(h, "memory", &u8) == ESP_OK && u8 <= 1) {
        settings.memory = u8;
    }
    nvs_close(h);
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "can't open NVS");
        return;
    }
    nvs_set_u8(h, "rom", settings.rom);
    nvs_set_u8(h, "memory", settings.memory);
    nvs_commit(h);
    nvs_close(h);
}
