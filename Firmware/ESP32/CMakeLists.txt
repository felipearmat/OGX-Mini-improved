cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

set(EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../external)
set(EXTERNAL_CMAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
set(BLUEPAD32_ROOT ${EXTERNAL_DIR}/bluepad32)
# BTstack's ESP-IDF port is used in place (BTstack_project.cmake below), the same way
# as Bluepad32's own examples/esp32 project.
set(ENV{BTSTACK_ROOT} ${BLUEPAD32_ROOT}/external/btstack)

include(${CMAKE_CURRENT_SOURCE_DIR}/../FWDefines.cmake)
include(${EXTERNAL_CMAKE_DIR}/init_submodules.cmake)
include(${EXTERNAL_CMAKE_DIR}/patch_libs.cmake)
include(${EXTERNAL_CMAKE_DIR}/generate_gatt_header.cmake)

init_git_submodules(${EXTERNAL_DIR}
    ${EXTERNAL_DIR}/bluepad32
    ${EXTERNAL_DIR}/libfixmath
)
apply_lib_patches(${EXTERNAL_DIR})
generate_gatt_header(
    $ENV{BTSTACK_ROOT}
    ${CMAKE_CURRENT_SOURCE_DIR}/../RP2040/src/BLEServer/att_delayed_response.gatt
    ${CMAKE_CURRENT_SOURCE_DIR}/main/BLEServer/att_delayed_response.h
    )

if(NOT EXISTS ${BLUEPAD32_ROOT}/src/components/bluepad32)
    message(FATAL_ERROR "External directory not found: ${BLUEPAD32_ROOT}/src/components/bluepad32")
endif()

set(EXTRA_COMPONENT_DIRS
    ${BLUEPAD32_ROOT}/src/components
    ${CMAKE_CURRENT_SOURCE_DIR}/components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include($ENV{BTSTACK_ROOT}/port/esp32/components/btstack/cmake/BTstack_project.cmake)
project("${FW_NAME}-${FW_VERSION}-ESP32")

idf_build_set_property(COMPILE_OPTIONS 
    "-w" 
    "-Wno-error"
    APPEND)
