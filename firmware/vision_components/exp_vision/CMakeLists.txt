# Camera preview component (EXP_VISION builds only; the top-level CMakeLists adds this directory conditionally).
# Reads frames from the official BSP camera module (mosaico_module_camera, left slot, DVP) and converts them with the PPA.
# No network, no radio.
idf_component_register(
    SRCS "dev_camera.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mosaico_module_camera esp_driver_ppa esp_mm esp_timer freertos heap log)
