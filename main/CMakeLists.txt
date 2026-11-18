# The web page's script: QL files put on cartridges and disks (main/web)
set(embed "")
if(CONFIG_EMU_WEB)
    list(APPEND embed "web/qlfiles.js")
endif()

idf_component_register(SRCS "main.c" "keymap.c" "settings.c" "rom_store.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES ${embed}
                       PRIV_REQUIRES emu ql esp_partition nvs_flash esp_psram)
