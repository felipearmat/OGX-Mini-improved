# Virtual adapter: the web app's side of the firmware (WebApp protocol, settings, flash) built for
# the PC. Serial port on stdin / stdout, flash in a file; the web app's Playwright suite drives it.
# Part of the host tests, or alone (no patched submodules needed):
#   cmake -S Firmware/RP2040/tests/virtual_adapter -B build-va && cmake --build build-va
cmake_minimum_required(VERSION 3.16)
if (NOT DEFINED FW_SRC)
    project(ogxm_virtual_adapter C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(FW_SRC ${CMAKE_CURRENT_LIST_DIR}/../../src)
    set(FW_COMMON ${CMAKE_CURRENT_LIST_DIR}/../../../common)
    set(EXTERNAL_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../external)
    set(MOCKS ${CMAKE_CURRENT_LIST_DIR}/../mocks)
    add_compile_options(-Wall -Wextra)
    set(FIX ${EXTERNAL_DIR}/libfixmath/libfixmath)
    add_library(fixmath STATIC ${FIX}/fix16.c ${FIX}/fix16_exp.c ${FIX}/fix16_sqrt.c ${FIX}/fix16_str.c
        ${FIX}/fix16_trig.c ${FIX}/uint32.c)
    target_include_directories(fixmath PUBLIC ${EXTERNAL_DIR}/libfixmath)
endif()

set(VA ${CMAKE_CURRENT_LIST_DIR})
add_executable(virtual_adapter
    ${VA}/main.cpp
    ${VA}/virtual_board.cpp
    ${FW_SRC}/USBDevice/DeviceDriver/WebApp/WebApp.cpp
    ${FW_SRC}/UserSettings/UserSettings.cpp
    ${FW_SRC}/USBDevice/DeviceDriver/KBM/KbmSettings.cpp
    ${FW_SRC}/Diagnostics/Diagnostics.cpp
    ${FW_SRC}/Diagnostics/DiagnosticsBoard.cpp
    ${FW_COMMON}/Diagnostics/DiagNames.cpp
    ${FW_COMMON}/UserSettings/UserProfile.cpp
    ${FW_COMMON}/UserSettings/JoystickSettings.cpp
    ${FW_COMMON}/UserSettings/TriggerSettings.cpp
    ${FW_COMMON}/UserSettings/DongleSettings.cpp
    ${FW_COMMON}/Board/ModeIndicator.cpp)
target_include_directories(virtual_adapter PRIVATE ${VA}/mocks ${MOCKS} ${FW_SRC} ${FW_COMMON}
    ${EXTERNAL_DIR}/tinyusb/src)
# A Pico 2 W with the external status LED on GP6 and the wired USB port.
target_compile_definitions(virtual_adapter PRIVATE CONFIG_OGXM_BOARD_PI_PICO2W=1 CONFIG_EN_BLUETOOTH=1
    CONFIG_EN_RGB=1 OGXM_EXT_RGB_PIN=6 OGXM_EXT_RGB_COUNT=1 CONFIG_EN_USB_HOST=1 MAX_GAMEPADS=1 NVS_SECTORS=4
    CFG_TUSB_MCU=OPT_MCU_NONE BOARD_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED BOARD_TUH_MAX_SPEED=OPT_MODE_FULL_SPEED
    TUP_DCD_ENDPOINT_MAX=8 OGXM_VERSION="virtual" BUILD_DATETIME="virtual"
    OGXM_BOARD_NAME="PI_PICO2W_VIRTUAL" PICO_RP2350=1)
target_compile_options(virtual_adapter PRIVATE -Wno-unused-parameter $<$<CXX_COMPILER_ID:GNU>:-Wno-volatile> $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-deprecated-volatile>)
target_link_libraries(virtual_adapter PRIVATE fixmath)
