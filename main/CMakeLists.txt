idf_component_register(SRCS "main.c" "keymap.c" "settings.c" "rom_store.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES emu ql esp_partition nvs_flash esp_psram)
