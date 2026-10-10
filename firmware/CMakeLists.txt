# ESP-Mosaico Experience firmware, Route B: a standalone ESP-IDF project on top of the official BSP.
# It deliberately has NO Vibe Mode / Recovery / Iris integration and NO radio components.
cmake_minimum_required(VERSION 3.22)

# Offline speech control (stage 3e): idf.py -B build_voice -DEXP_VOICE=1 build
# It needs a different partition table (a "model" partition for the ESP-SR models), so it uses its OWN build directory,
# sdkconfig and dependency lock; the default build and its sdkconfig / partition table stay untouched.
option(EXP_VOICE "Offline speech control (ESP-SR). Needs the voice partition table: see docs/11_VOICE_SETUP.md" OFF)

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

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(EXP_VOICE)
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.voice.lock")
endif()
project(mosaico_experience VERSION 0.3.0)

if(EXP_VOICE)
    # esp-dl (pulled in by esp-sr) is C++ that newer GCC releases flag with -Wnon-c-typedef-for-linkage; the owner's
    # Full Demo needed the same exception. Applied only when the compiler knows the option and the target exists.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-Wno-error=non-c-typedef-for-linkage" EXP_HAVE_NONC_TYPEDEF_FLAG)
    idf_component_get_property(exp_dl_lib espressif__esp-dl COMPONENT_LIB)
    if(EXP_HAVE_NONC_TYPEDEF_FLAG AND exp_dl_lib)
        target_compile_options(${exp_dl_lib} PRIVATE -Wno-error=non-c-typedef-for-linkage)
    endif()
endif()
