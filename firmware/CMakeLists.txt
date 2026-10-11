# ESP-Mosaico Experience firmware, Route B: a standalone ESP-IDF project on top of the official BSP.
# It deliberately has NO Vibe Mode / Recovery / Iris integration and NO radio components.
cmake_minimum_required(VERSION 3.22)

# Offline speech control (stage 3e): idf.py -B build_voice -DEXP_VOICE=1 build
# It needs a different partition table (a "model" partition for the ESP-SR models), so it uses its OWN build directory,
# sdkconfig and dependency lock; the default build and its sdkconfig / partition table stay untouched.
option(EXP_VOICE "Offline speech control (ESP-SR). Needs the voice partition table: see docs/11_VOICE_SETUP.md" OFF)

# Camera preview (stage 4a): idf.py -B build_vision -DEXP_VISION=1 build
# Own build directory, sdkconfig and dependency lock as well. It reuses the partition table of the voice build (partitions_voice.csv:
# nvs / calibration-data / factory 5 MB / model 4 MB / coredump), so going between the voice and the vision image needs NO partition-table
# write: only the app at 0x10000 is replaced. The "model" partition is neither read nor written by the vision image.
# Voice and vision cannot be combined yet (stage 4c decides the partition layout for both).
option(EXP_VISION "Camera preview test view (BSP camera module, left slot). See docs/12_VISION_SETUP.md" OFF)
if(EXP_VISION AND EXP_VOICE)
    message(FATAL_ERROR "EXP_VOICE and EXP_VISION cannot be combined yet (stage 4c)")
endif()
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

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
if(EXP_VOICE)
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/voice_components")
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
    # The voice defaults replace the stock single-app partition table (a later "y" in a Kconfig choice is not reliable).
    file(READ "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults" exp_common_defaults)
    string(REPLACE "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y" "# CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE replaced by partitions_voice.csv" exp_common_defaults "${exp_common_defaults}")
    file(READ "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.voice" exp_voice_defaults)
    file(WRITE "${CMAKE_BINARY_DIR}/voice.defaults" "${exp_common_defaults}\n${exp_voice_defaults}")
    set(SDKCONFIG_DEFAULTS "${CMAKE_BINARY_DIR}/voice.defaults")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults" "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.voice")
endif()

if(EXP_VISION)
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/vision_components")
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
    # The camera pad (GPIO33 = D2) is the USB Serial/JTAG D- pad: the console moves to UART, as in the official camera examples.
    file(READ "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults" exp_common_defaults)
    string(REPLACE "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y" "# CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE replaced by partitions_voice.csv" exp_common_defaults "${exp_common_defaults}")
    string(REPLACE "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y" "# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG replaced (camera D2 shares that pad)" exp_common_defaults "${exp_common_defaults}")
    file(READ "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.vision" exp_vision_defaults)
    file(WRITE "${CMAKE_BINARY_DIR}/vision.defaults" "${exp_common_defaults}\n${exp_vision_defaults}")
    set(SDKCONFIG_DEFAULTS "${CMAKE_BINARY_DIR}/vision.defaults")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults" "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.vision")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(EXP_VOICE)
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.voice.lock")
endif()
if(EXP_VISION)
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
