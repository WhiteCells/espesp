# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

set(SDKCONFIG_DEFAULTS "sdkconfig.default")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(espesp)

# ESPMicroSpeechFeatures currently appends these two definitions as one C++ option.
# ESP-IDF 6 keeps the embedded space, which makes GCC see a duplicate USE_ESP32
# definition once micro_wake_word exports the same macro normally.
idf_build_replace_option_from_property(CXX_COMPILE_OPTIONS "-DUSE_ESP32 -DUSE_ESP_IDF" "")

if(TARGET __idf_micro_wake_word)
    get_target_property(_espesp_mww_sources __idf_micro_wake_word SOURCES)
    if(_espesp_mww_sources)
        list(FILTER _espesp_mww_sources EXCLUDE REGEX "esphome/components/i2s_audio/.*\\.cpp$")
        set_property(TARGET __idf_micro_wake_word PROPERTY SOURCES "${_espesp_mww_sources}")
    endif()
    target_include_directories(__idf_micro_wake_word PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/main/micro_wake_word/compat_include"
    )
endif()
