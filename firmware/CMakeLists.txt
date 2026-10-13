# ESP-Mosaico Experience firmware, Route B: a standalone ESP-IDF project on top of the official BSP.
# It deliberately has NO Vibe Mode / Recovery / Iris integration and NO radio components.
cmake_minimum_required(VERSION 3.22)

# Offline speech control (stage 3e): idf.py -B build_voice -DEXP_VOICE=1 build
# It needs a different partition table (a "model" partition for the ESP-SR models), so it uses its OWN build directory,
# sdkconfig and dependency lock; the default build and its sdkconfig / partition table stay untouched.
option(EXP_VOICE "Offline speech control (ESP-SR). Needs the voice partition table: see docs/11_VOICE_SETUP.md" OFF)

# Camera & face tracking (stage 4a/4b/4c): idf.py -B build_vision -DEXP_VISION=1 build
# Own build directory, sdkconfig and dependency lock as well. It reuses the partition table of the voice build (partitions_voice.csv:
# nvs / calibration-data / factory 5 MB / model 4 MB / coredump), so going between the voice and the vision image needs NO partition-table
# write: only the app at 0x10000 is replaced. Can also be combined with -DEXP_VOICE=1.
option(EXP_VISION "Camera preview and face tracking (BSP camera module, left slot). See docs/12_VISION_SETUP.md" OFF)
if(EXP_VISION)
    set(ENV{EXP_VISION} "1")
else()
    set(ENV{EXP_VISION} "0")
endif()

# The component requirements are collected in a separate CMake process that does not see -D cache variables, but it does
# inherit the environment: main/CMakeLists.txt therefore reads $ENV{EXP_VOICE} (same trick as the owner's Full Demo).
if(EXP_VOICE)
    set(ENV{EXP_VOICE} "1")
else()
    set(ENV{EXP_VOICE} "0")
endif()

set(ESP_IRIS_BUILD_PROFILE "usb" CACHE STRING "Iris dependency profile: USB CDC0 only, no TCP/lwIP/mDNS" FORCE)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/../workspace/esp-mosaico-vibe/submodule/esp-mosaico-utils/ESP-Iris/components/esp_iris")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
if(EXP_VOICE OR EXP_VISION)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
    file(READ "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults" exp_combined_defaults)
    string(REPLACE "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y" "# CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE replaced by partitions_voice.csv" exp_combined_defaults "${exp_combined_defaults}")
    if(EXP_VOICE)
        list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/voice_components")
        file(READ "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.voice" exp_voice_defaults)
        string(APPEND exp_combined_defaults "\n${exp_voice_defaults}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.voice")
    endif()
    if(EXP_VISION)
        list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/vision_components")
        # The camera pad (GPIO33 = D2) is the USB Serial/JTAG D- pad: the console moves to UART, as in the official camera examples.
        string(REPLACE "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y" "# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG replaced (camera D2 shares that pad)" exp_combined_defaults "${exp_combined_defaults}")
        file(READ "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.vision" exp_vision_defaults)
        string(APPEND exp_combined_defaults "\n${exp_vision_defaults}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.vision")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/combined.defaults" "${exp_combined_defaults}")
    set(SDKCONFIG_DEFAULTS "${CMAKE_BINARY_DIR}/combined.defaults")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(EXP_VOICE AND NOT EXP_VISION)
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.voice.lock")
elseif(EXP_VISION AND NOT EXP_VOICE)
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.vision.lock")
endif()
project(mosaico_experience VERSION 0.3.0)

if(EXP_VOICE OR EXP_VISION)
    # esp-dl (pulled in by esp-sr) is C++ that newer GCC releases flag with -Wnon-c-typedef-for-linkage; the owner's
    # Full Demo needed the same exception. Applied only when the compiler knows the option and the target exists.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-Wno-error=non-c-typedef-for-linkage" EXP_HAVE_NONC_TYPEDEF_FLAG)
    idf_component_get_property(exp_dl_lib espressif__esp-dl COMPONENT_LIB)
    if(EXP_HAVE_NONC_TYPEDEF_FLAG AND exp_dl_lib)
        target_compile_options(${exp_dl_lib} PRIVATE -Wno-error=non-c-typedef-for-linkage)
    endif()
endif()
