# Host-side unit tests (OGX-Mini-improved). Built with the native compiler, not the Pico SDK:
#   cmake -S Firmware/RP2040/tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.16)
project(ogxm_host_tests C CXX)

set(CMAKE_CXX_STANDARD 20)  # same as the firmware (Range.h uses requires-clauses)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(FW_SRC ${CMAKE_CURRENT_LIST_DIR}/../src)
set(EXTERNAL_DIR ${CMAKE_CURRENT_LIST_DIR}/../../external)
set(MOCKS ${CMAKE_CURRENT_LIST_DIR}/mocks)

enable_testing()

add_compile_options(-Wall -Wextra)

# libfixmath (used by Gamepad.h)
add_library(fixmath STATIC
    ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16.c
    ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16_exp.c
    ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16_sqrt.c
    ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16_str.c
    ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16_trig.c
    ${EXTERNAL_DIR}/libfixmath/libfixmath/uint32.c
)
target_include_directories(fixmath PUBLIC ${EXTERNAL_DIR}/libfixmath)

function(ogxm_add_test NAME)
    add_executable(${NAME} ${ARGN})
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${MOCKS} ${FW_SRC})
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

ogxm_add_test(test_mode_indicator test_mode_indicator.cpp ${FW_SRC}/Custom/ModeIndicator.cpp)

ogxm_add_test(test_nvstool test_nvstool.cpp)
target_compile_definitions(test_nvstool PRIVATE NVS_SECTORS=4)

ogxm_add_test(test_wire_pad test_wire_pad.cpp)
target_compile_definitions(test_wire_pad PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1)
target_link_libraries(test_wire_pad PRIVATE fixmath)

ogxm_add_test(test_joycon_settings test_joycon_settings.cpp ${FW_SRC}/Custom/JoyConSettings.cpp ${FW_SRC}/Custom/DongleSettings.cpp)

ogxm_add_test(test_dongle_settings test_dongle_settings.cpp ${FW_SRC}/Custom/DongleSettings.cpp)

ogxm_add_test(test_kbm test_kbm.cpp ${FW_SRC}/Custom/KbmSettings.cpp ${FW_SRC}/Custom/KbmMapper.cpp)

ogxm_add_test(test_switch_pro_layout test_switch_pro_layout.cpp)

ogxm_add_test(test_hd_rumble test_hd_rumble.cpp ${FW_SRC}/USBDevice/DeviceDriver/Switch/HdRumble.cpp)

ogxm_add_test(test_sony_imu test_sony_imu.cpp)

ogxm_add_test(test_sony_reports test_sony_reports.cpp)

ogxm_add_test(test_gamepad_out test_gamepad_out.cpp)
target_compile_definitions(test_gamepad_out PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1)
target_link_libraries(test_gamepad_out PRIVATE fixmath)

ogxm_add_test(test_reported_mac test_reported_mac.cpp ${FW_SRC}/Custom/ReportedMac.cpp ${FW_SRC}/Custom/DongleSettings.cpp)

ogxm_add_test(test_switch_imu test_switch_imu.cpp ${FW_SRC}/Custom/SwitchImu.cpp)
target_compile_definitions(test_switch_imu PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1)
target_link_libraries(test_switch_imu PRIVATE fixmath)

# USB device drivers (DInput, PS3, PS4, STEAM) built from the firmware sources, with the TinyUSB
# calls stubbed in the test: output reports in through set_report_cb, input reports captured.
set(DEV ${FW_SRC}/USBDevice/DeviceDriver)
ogxm_add_test(test_device_reports test_device_reports.cpp
    ${DEV}/DInput/DInput.cpp
    ${DEV}/PS3/PS3.cpp
    ${DEV}/PS4/PS4.cpp
    ${DEV}/Steam/Steam.cpp
    ${DEV}/Steam/SteamTouchpad.cpp
    ${DEV}/KBM/KBM.cpp
    ${FW_SRC}/Custom/KbmSettings.cpp
    ${FW_SRC}/Custom/KbmMapper.cpp
    ${FW_SRC}/UserSettings/UserProfile.cpp
    ${FW_SRC}/UserSettings/JoystickSettings.cpp
    ${FW_SRC}/UserSettings/TriggerSettings.cpp
    ${FW_SRC}/Custom/DongleSettings.cpp
    ${FW_SRC}/Custom/ReportedMac.cpp)
target_include_directories(test_device_reports PRIVATE ${EXTERNAL_DIR}/tinyusb/src)
target_compile_definitions(test_device_reports PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1 MAX_GAMEPADS=1
    CFG_TUSB_MCU=OPT_MCU_NONE BOARD_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED BOARD_TUH_MAX_SPEED=OPT_MODE_FULL_SPEED)
target_compile_definitions(test_device_reports PRIVATE TUP_DCD_ENDPOINT_MAX=8)
# Warnings from the TinyUSB headers and unused virtual parameters, not from the code under test.
target_compile_options(test_device_reports PRIVATE -Wno-unused-parameter -Wno-volatile)
target_link_libraries(test_device_reports PRIVATE fixmath)

# Bluepad32 Switch parser with the OGX-Mini-improved patch series (applied here if needed). Built 32-bit so struct layouts match the RP2350 (the parser's instance
# must fit in 256 bytes of parser_data).
set(BP32 ${EXTERNAL_DIR}/bluepad32/src/components/bluepad32)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/patch_libs.cmake)
ogxm_apply_switch_parser_patches(${EXTERNAL_DIR})  # same series as the firmware build
ogxm_apply_output_queue_patch(${EXTERNAL_DIR})
ogxm_add_test(test_output_queue test_output_queue.cpp ${BP32}/uni_circular_buffer.c)
target_include_directories(test_output_queue PRIVATE ${BP32}/include)
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS -m32)
check_c_source_compiles("int main(void){return 0;}" OGXM_HAVE_M32)
unset(CMAKE_REQUIRED_FLAGS)
if (OGXM_HAVE_M32)
    add_executable(test_switch_parser
        switch_parser/test_switch_parser.c
        switch_parser/fake_bt.c
        ${BP32}/parser/uni_hid_parser_switch.c)
    target_include_directories(test_switch_parser PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/switch_parser ${BP32}/include ${BP32}
        ${EXTERNAL_DIR}/bluepad32/external/btstack/src ${FW_SRC})
    target_compile_definitions(test_switch_parser PRIVATE MAX_GAMEPADS=1 CONFIG_OGXM_BOARD_PI_PICO2W=1)
    target_compile_options(test_switch_parser PRIVATE -m32 -Wno-unused-parameter -Wno-sign-compare)
    target_link_options(test_switch_parser PRIVATE -m32)
    add_test(NAME test_switch_parser COMMAND test_switch_parser)
else ()
    message(WARNING "No 32-bit toolchain (install gcc-multilib): skipping test_switch_parser")
endif ()
