# Debug aid: keeps the last ESP_LOG lines in RAM so they can be shown on the panel (the USB-C port has no
# console while the app runs). Pure software, no radio. Not part of the exhibition build (EXP_DBGLOG=0).
idf_component_register(
    SRCS "dbglog_ring.c" "exp_dbglog.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log esp_system freertos)
