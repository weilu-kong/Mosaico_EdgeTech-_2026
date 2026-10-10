# Offline speech component (EXP_VOICE builds only; the top-level CMakeLists adds this directory conditionally).
idf_component_register(
    SRCS "dev_voice.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES espressif__esp-sr esp_timer esp_system freertos heap log)
