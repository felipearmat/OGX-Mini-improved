# Host-side unit tests (OGX-Mini-improved). Built with the native compiler, not the Pico SDK:
#   cmake -S Firmware/RP2040/tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
cmake_minimum_required(VERSION 3.16)
project(ogxm_host_tests C CXX)

set(CMAKE_CXX_STANDARD 20)  # same as the firmware (Range.h uses requires-clauses)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(FW_SRC ${CMAKE_CURRENT_LIST_DIR}/../src)
set(FW_COMMON ${CMAKE_CURRENT_LIST_DIR}/../../common)
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
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${MOCKS} ${FW_SRC} ${FW_COMMON})
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

ogxm_add_test(test_mode_indicator test_mode_indicator.cpp ${FW_COMMON}/Board/ModeIndicator.cpp
              ${FW_COMMON}/Board/StatusLed.cpp)

# Custom: what the web app must agree on (Firmware/common/contract/webapp_contract.json).
ogxm_add_test(test_webapp_contract test_webapp_contract.cpp ${FW_COMMON}/UserSettings/DongleSettings.cpp
              ${FW_COMMON}/Board/ModeIndicator.cpp ${FW_COMMON}/UserSettings/UserProfile.cpp
              ${FW_SRC}/USBDevice/DeviceDriver/KBM/KbmSettings.cpp)
target_link_libraries(test_webapp_contract PRIVATE fixmath)
target_compile_definitions(test_webapp_contract PRIVATE
    OGXM_CONTRACT_PATH="${FW_COMMON}/contract/webapp_contract.json")

ogxm_add_test(test_nvstool test_nvstool.cpp)
target_compile_definitions(test_nvstool PRIVATE NVS_SECTORS=4)

# I2C link of the RP2040 + ESP32 hybrid: wire format shared by both firmwares (Firmware/common)
ogxm_add_test(test_esp32_bridge test_esp32_bridge.cpp ${FW_COMMON}/UserSettings/DongleSettings.cpp
    ${FW_COMMON}/UserSettings/UserProfile.cpp ${FW_COMMON}/UserSettings/JoystickSettings.cpp
    ${FW_COMMON}/UserSettings/TriggerSettings.cpp)
target_link_libraries(test_esp32_bridge PRIVATE fixmath)

ogxm_add_test(test_wire_pad test_wire_pad.cpp)
target_compile_definitions(test_wire_pad PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1)
target_link_libraries(test_wire_pad PRIVATE fixmath)

ogxm_add_test(test_joycon_settings test_joycon_settings.cpp ${FW_COMMON}/Bluepad32/JoyConSettings.cpp ${FW_COMMON}/UserSettings/DongleSettings.cpp)

ogxm_add_test(test_dongle_settings test_dongle_settings.cpp ${FW_COMMON}/UserSettings/DongleSettings.cpp)

ogxm_add_test(test_diagnostics test_diagnostics.cpp ${FW_SRC}/Diagnostics/Diagnostics.cpp ${FW_COMMON}/Diagnostics/DiagNames.cpp
              ${FW_SRC}/Board/LogRing.cpp ${FW_COMMON}/UserSettings/DongleSettings.cpp)

# Custom: the kept log and its GET_LOG reads (Board/LogRing.h).
ogxm_add_test(test_log_ring test_log_ring.cpp ${FW_SRC}/Board/LogRing.cpp)

# Custom: the hybrid's ESP32 output received by DMA (Board/RxRing.h).
ogxm_add_test(test_rx_ring test_rx_ring.cpp)

# Custom: the kept log in flash (Board/FlashLog.h), on the RAM flash of mocks/.
ogxm_add_test(test_flash_log test_flash_log.cpp ${FW_SRC}/Board/FlashLog.cpp ${FW_SRC}/Board/LogRing.cpp
              ${FW_SRC}/Diagnostics/Diagnostics.cpp ${FW_COMMON}/Diagnostics/DiagNames.cpp
              ${FW_COMMON}/UserSettings/DongleSettings.cpp)
target_compile_definitions(test_flash_log PRIVATE NVS_SECTORS=4 OGXM_FLASH_LOG_SECTORS=4)

ogxm_add_test(test_spin_lock_budget test_spin_lock_budget.cpp)

ogxm_add_test(test_scan_policy test_scan_policy.cpp)
# Shared by every Bluepad32 board (Firmware/common/Bluepad32, Firmware/common/Gamepad/GamepadMapper.h)
ogxm_add_test(test_bt_shared test_bt_shared.cpp ${FW_COMMON}/UserSettings/UserProfile.cpp
    ${FW_COMMON}/UserSettings/JoystickSettings.cpp ${FW_COMMON}/UserSettings/TriggerSettings.cpp
    ${FW_COMMON}/UserSettings/DongleSettings.cpp)
target_link_libraries(test_bt_shared PRIVATE fixmath)
target_compile_definitions(test_bt_shared PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1)
ogxm_add_test(test_idle_motion test_idle_motion.cpp)
target_compile_definitions(test_spin_lock_budget PRIVATE OGXM_FW_SRC="${FW_SRC}")

ogxm_add_test(test_kbm test_kbm.cpp ${FW_SRC}/USBDevice/DeviceDriver/KBM/KbmSettings.cpp ${FW_SRC}/USBDevice/DeviceDriver/KBM/KbmMapper.cpp)

ogxm_add_test(test_switch_pro_layout test_switch_pro_layout.cpp)

ogxm_add_test(test_hd_rumble test_hd_rumble.cpp ${FW_SRC}/USBDevice/DeviceDriver/Switch/HdRumble.cpp)

ogxm_add_test(test_sony_imu test_sony_imu.cpp)
# Custom: DS4 / DualSense battery and charging state (Gamepad/SonyBattery.h).
ogxm_add_test(test_sony_battery test_sony_battery.cpp)
# Custom: every motion source at the same units in the Sony outputs (Gamepad/MotionImu.h).
ogxm_add_test(test_motion_units test_motion_units.cpp ${FW_COMMON}/Bluepad32/JoyConSettings.cpp ${FW_COMMON}/UserSettings/DongleSettings.cpp)
target_compile_definitions(test_motion_units PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1)
target_link_libraries(test_motion_units PRIVATE fixmath)

ogxm_add_test(test_sony_reports test_sony_reports.cpp)

ogxm_add_test(test_gamepad_out test_gamepad_out.cpp
    ${FW_COMMON}/UserSettings/UserProfile.cpp
    ${FW_COMMON}/UserSettings/JoystickSettings.cpp
    ${FW_COMMON}/UserSettings/TriggerSettings.cpp)
target_compile_definitions(test_gamepad_out PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1)
target_link_libraries(test_gamepad_out PRIVATE fixmath)

ogxm_add_test(test_reported_mac test_reported_mac.cpp ${FW_SRC}/Board/ReportedMac.cpp ${FW_COMMON}/UserSettings/DongleSettings.cpp)

ogxm_add_test(test_switch_imu test_switch_imu.cpp ${FW_SRC}/USBDevice/DeviceDriver/Switch/SwitchImu.cpp)
target_compile_definitions(test_switch_imu PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1)
target_link_libraries(test_switch_imu PRIVATE fixmath)
# Custom: the Switch output's emulated SPI flash (USBDevice/DeviceDriver/Switch/SwitchSpi.h).
ogxm_add_test(test_switch_spi test_switch_spi.cpp ${FW_SRC}/USBDevice/DeviceDriver/Switch/SwitchSpi.cpp
              ${FW_SRC}/USBDevice/DeviceDriver/Switch/SwitchImu.cpp)
target_compile_definitions(test_switch_spi PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1)
target_link_libraries(test_switch_spi PRIVATE fixmath)

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
    ${FW_SRC}/USBDevice/DeviceDriver/KBM/KbmSettings.cpp
    ${FW_SRC}/USBDevice/DeviceDriver/KBM/KbmMapper.cpp
    ${FW_COMMON}/UserSettings/UserProfile.cpp
    ${FW_COMMON}/UserSettings/JoystickSettings.cpp
    ${FW_COMMON}/UserSettings/TriggerSettings.cpp
    ${FW_COMMON}/UserSettings/DongleSettings.cpp
    ${FW_SRC}/Board/ReportedMac.cpp)
target_include_directories(test_device_reports PRIVATE ${EXTERNAL_DIR}/tinyusb/src)
target_compile_definitions(test_device_reports PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1 MAX_GAMEPADS=1
    CFG_TUSB_MCU=OPT_MCU_NONE BOARD_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED BOARD_TUH_MAX_SPEED=OPT_MODE_FULL_SPEED)
target_compile_definitions(test_device_reports PRIVATE TUP_DCD_ENDPOINT_MAX=8)
# Warnings from the TinyUSB headers and unused virtual parameters, not from the code under test.
target_compile_options(test_device_reports PRIVATE -Wno-unused-parameter -Wno-volatile)
target_link_libraries(test_device_reports PRIVATE fixmath)

# Custom: the web app's side of the firmware built for the PC (virtual_adapter/CMakeLists.txt).
add_subdirectory(virtual_adapter)

# Bluepad32 Switch parser with the OGX-Mini-improved patch series (applied here if needed). Built 32-bit so struct layouts match the RP2350 (the parser's instance
# must fit in 256 bytes of parser_data).
set(BP32 ${EXTERNAL_DIR}/bluepad32/src/components/bluepad32)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/patch_libs.cmake)
ogxm_patch_btstack(${EXTERNAL_DIR})    # the same patches (and stamps) as the firmware builds
ogxm_patch_bluepad32(${EXTERNAL_DIR})
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
        ${BP32}/parser/uni_hid_parser_switch.c
        ${BP32}/parser/uni_hid_parser_rumble.c)
    target_include_directories(test_switch_parser PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/switch_parser ${BP32}/include ${BP32}
        ${EXTERNAL_DIR}/bluepad32/external/btstack/src ${FW_SRC} ${FW_COMMON})
    target_compile_definitions(test_switch_parser PRIVATE MAX_GAMEPADS=1 CONFIG_OGXM_BOARD_PI_PICO2W=1)
    target_compile_options(test_switch_parser PRIVATE -m32 -Wno-unused-parameter -Wno-sign-compare)
    target_link_options(test_switch_parser PRIVATE -m32)
    add_test(NAME test_switch_parser COMMAND test_switch_parser)

    # End-to-end replay of recorded controller input (fixtures/README.md): Bluepad32's parsers,
    # the firmware's PadIn path and the output drivers, all 32-bit.
    add_library(fixmath32 STATIC
        ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16.c
        ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16_exp.c
        ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16_sqrt.c
        ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16_str.c
        ${EXTERNAL_DIR}/libfixmath/libfixmath/fix16_trig.c
        ${EXTERNAL_DIR}/libfixmath/libfixmath/uint32.c)
    target_include_directories(fixmath32 PUBLIC ${EXTERNAL_DIR}/libfixmath)
    target_compile_options(fixmath32 PRIVATE -m32)
    add_executable(test_fixtures
        fixtures/test_fixtures.cpp
        fixtures/replay_bt.cpp
        fixtures/bp32_stubs.c
        switch_parser/fake_bt.c
        ${BP32}/parser/uni_hid_parser_switch.c
        ${BP32}/parser/uni_hid_parser_rumble.c
        ${BP32}/parser/uni_hid_parser_ds4.c
        ${BP32}/parser/uni_hid_parser_ds5.c
        ${BP32}/uni_utils.c
        ${DEV}/DInput/DInput.cpp
        ${DEV}/PS4/PS4.cpp
        ${DEV}/Steam/Steam.cpp
        ${DEV}/Steam/SteamTouchpad.cpp
        ${DEV}/Switch/Switch.cpp
        ${DEV}/Switch/HdRumble.cpp
        ${DEV}/Switch/SwitchImu.cpp
        ${DEV}/Switch/SwitchSpi.cpp
        ${DEV}/XInput/XInput.cpp
        ${DEV}/PS3/PS3.cpp
        ${DEV}/XboxOG/XboxOG_GP.cpp
        ${DEV}/WiiU/WiiU.cpp
        ${DEV}/PSClassic/PSClassic.cpp
        ${DEV}/XboxOG/XboxOG_SB.cpp
        ${DEV}/XboxOG/XboxOG_XR.cpp
        ${DEV}/KBM/KBM.cpp
        ${DEV}/KBM/KbmSettings.cpp
        ${DEV}/KBM/KbmMapper.cpp
        ${DEV}/GameCube/GameCube.cpp
        ${DEV}/N64/N64.cpp
        ${DEV}/PS1PS2/PS1PS2.cpp
        fixtures/console_outputs.cpp
        ${FW_SRC}/Wii/WiiReportConverter.cpp
        ${EXTERNAL_DIR}/libxsm3/xsm3.c
        ${EXTERNAL_DIR}/libxsm3/usbdsec.c
        ${EXTERNAL_DIR}/libxsm3/excrypt_des.c
        ${EXTERNAL_DIR}/libxsm3/excrypt_parve.c
        ${EXTERNAL_DIR}/libxsm3/excrypt_sha.c
        ${FW_COMMON}/UserSettings/UserProfile.cpp
        ${FW_COMMON}/UserSettings/JoystickSettings.cpp
        ${FW_COMMON}/UserSettings/TriggerSettings.cpp
        ${FW_COMMON}/UserSettings/DongleSettings.cpp
        ${FW_COMMON}/Bluepad32/JoyConSettings.cpp
        ${FW_SRC}/Board/ReportedMac.cpp)
    target_include_directories(test_fixtures PRIVATE
        ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/fixtures ${CMAKE_CURRENT_LIST_DIR}/fixtures/stubs ${CMAKE_CURRENT_LIST_DIR}/switch_parser ${MOCKS} ${BP32}/include ${BP32}
        ${EXTERNAL_DIR}/bluepad32/external/btstack/src ${EXTERNAL_DIR}/tinyusb/src ${EXTERNAL_DIR}/libxsm3 ${FW_SRC} ${FW_COMMON})
    target_compile_definitions(test_fixtures PRIVATE MAX_GAMEPADS=1 CONFIG_OGXM_BOARD_PI_PICO2W=1
        CFG_TUSB_MCU=OPT_MCU_NONE BOARD_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED BOARD_TUH_MAX_SPEED=OPT_MODE_FULL_SPEED
        TUP_DCD_ENDPOINT_MAX=8 OGXM_FIXTURES_DIR="${CMAKE_CURRENT_LIST_DIR}/fixtures")
    target_compile_options(test_fixtures PRIVATE -m32 -Wno-unused-parameter -Wno-sign-compare
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-volatile>)
    target_link_options(test_fixtures PRIVATE -m32)
    target_link_libraries(test_fixtures PRIVATE fixmath32)
    # The Wiimote converter is built for the boards with a USB host port (Wii mode takes USB pads).
    set_source_files_properties(${FW_SRC}/Wii/WiiReportConverter.cpp PROPERTIES COMPILE_DEFINITIONS CONFIG_EN_USB_HOST=1)
    add_test(NAME test_fixtures COMMAND test_fixtures)
else ()
    message(WARNING "No 32-bit toolchain (install gcc-multilib): skipping test_switch_parser")
endif ()
