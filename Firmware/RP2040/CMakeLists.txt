cmake_minimum_required(VERSION 3.13)

include(${CMAKE_CURRENT_LIST_DIR}/../FWDefines.cmake)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC ${CMAKE_CURRENT_LIST_DIR}/src)
# Sources shared with the ESP32 companion firmware (Firmware/common/README.md)
set(COMMON ${CMAKE_CURRENT_LIST_DIR}/../common)
set(EXTERNAL_DIR ${CMAKE_CURRENT_LIST_DIR}/../external)
set(PICOSDK_VERSION_TAG "2.3.1")

# Default Pico SDK path when not set (e.g. by CMake Tools or command line)
if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH ${EXTERNAL_DIR}/pico-sdk CACHE PATH "Path to Pico SDK")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/init_submodules.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/patch_libs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/generate_gatt_header.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/get_pico_sdk.cmake)

set(PICO_PIO_USB_PATH ${EXTERNAL_DIR}/Pico-PIO-USB)
set(PICO_TINYUSB_PATH ${EXTERNAL_DIR}/tinyusb)
set(BLUEPAD32_ROOT ${EXTERNAL_DIR}/bluepad32)
set(BTSTACK_ROOT ${BLUEPAD32_ROOT}/external/btstack)
set(PICO_BTSTACK_PATH ${BTSTACK_ROOT})
set(LIBFIXMATH_PATH ${EXTERNAL_DIR}/libfixmath)

#get_pico_sdk(${EXTERNAL_DIR} ${PICOSDK_VERSION_TAG})
init_git_submodules(${EXTERNAL_DIR}
    ${BLUEPAD32_ROOT}
    ${PICO_TINYUSB_PATH}
)
apply_lib_patches(${EXTERNAL_DIR})

set(SOURCES_BOARD 
    ${SRC}/main.cpp

    ${SRC}/OGXMini/OGXMini.cpp
    ${SRC}/OGXMini/Board/Standard.cpp
    ${SRC}/OGXMini/Board/PicoW.cpp
    ${SRC}/OGXMini/Board/Four_Channel_I2C.cpp
    ${SRC}/OGXMini/Board/ESP32_Bluepad32_I2C.cpp
    ${SRC}/OGXMini/Board/ESP32_Blueretro_I2C.cpp

    ${SRC}/TaskQueue/TaskQueue.cpp

    ${SRC}/Input/InputSlot.cpp

    ${SRC}/Board/ogxm_log.cpp
    ${SRC}/Board/getentropy.c
    ${SRC}/Board/esp32_api.cpp
    ${SRC}/Board/board_api.cpp
    ${SRC}/Board/board_api_private/board_api_led.cpp
    ${SRC}/Board/board_api_private/board_api_rgb.cpp
    ${COMMON}/Board/ModeIndicator.cpp
    ${COMMON}/Board/StatusLed.cpp
    ${SRC}/Board/board_api_private/board_api_bt.cpp
    ${SRC}/Board/board_api_private/board_api_usbh.cpp
    
    ${SRC}/UserSettings/UserSettings.cpp
    ${COMMON}/UserSettings/UserProfile.cpp
    ${COMMON}/UserSettings/JoystickSettings.cpp
    ${COMMON}/UserSettings/TriggerSettings.cpp

    ${SRC}/USBDevice/tud_callbacks.cpp
    ${SRC}/USBDevice/DeviceManager.cpp
    ${SRC}/USBDevice/DeviceDriver/DeviceDriver.cpp
    ${SRC}/USBDevice/DeviceDriver/PSClassic/PSClassic.cpp
    ${SRC}/USBDevice/DeviceDriver/PS3/PS3.cpp
    ${SRC}/USBDevice/DeviceDriver/PS4/PS4.cpp
    ${SRC}/USBDevice/DeviceDriver/Steam/Steam.cpp
    ${SRC}/USBDevice/DeviceDriver/Steam/SteamTouchpad.cpp
    ${SRC}/USBDevice/DeviceDriver/KBM/KBM.cpp
    ${SRC}/USBDevice/DeviceDriver/KBM/KbmSettings.cpp
    ${SRC}/USBDevice/DeviceDriver/KBM/KbmMapper.cpp
    ${SRC}/USBDevice/DeviceDriver/Switch/Switch.cpp
    ${SRC}/USBDevice/DeviceDriver/Switch/SwitchImu.cpp
    ${SRC}/USBDevice/DeviceDriver/Switch/SwitchSpi.cpp
    ${SRC}/USBDevice/DeviceDriver/Switch/HdRumble.cpp
    ${COMMON}/UserSettings/DongleSettings.cpp
    ${SRC}/Diagnostics/Diagnostics.cpp
    ${SRC}/Board/LogRing.cpp
    ${SRC}/Board/FlashLog.cpp
    ${COMMON}/Diagnostics/DiagNames.cpp
    ${SRC}/Diagnostics/DiagnosticsBoard.cpp
    ${SRC}/Diagnostics/CrashHandler.cpp
    ${SRC}/Board/ReportedMac.cpp
    ${SRC}/USBDevice/DeviceDriver/WiiU/WiiU.cpp
    ${SRC}/USBDevice/DeviceDriver/Wii/Wii.cpp
    ${SRC}/USBDevice/DeviceDriver/PS1PS2/PS1PS2.cpp
    ${SRC}/USBDevice/DeviceDriver/PS1PS2/controller_simulator.c
    ${SRC}/USBDevice/DeviceDriver/GameCube/GameCube.cpp
    ${SRC}/USBDevice/DeviceDriver/GameCube/gamecube_device.c
    ${SRC}/USBDevice/DeviceDriver/Dreamcast/Dreamcast.cpp
    ${SRC}/USBDevice/DeviceDriver/N64/N64.cpp
    ${SRC}/USBDevice/DeviceDriver/N64/n64_device.c
    ${SRC}/USBDevice/DeviceDriver/XInput/XInput.cpp
    ${SRC}/USBDevice/DeviceDriver/XboxOG/XboxOG_GP.cpp
    ${SRC}/USBDevice/DeviceDriver/XboxOG/XboxOG_SB.cpp
    ${SRC}/USBDevice/DeviceDriver/XboxOG/XboxOG_XR.cpp
    ${SRC}/USBDevice/DeviceDriver/DInput/DInput.cpp
    ${SRC}/USBDevice/DeviceDriver/DInput/DInputDebug.cpp
    ${SRC}/USBDevice/DeviceDriver/WebApp/WebApp.cpp
    ${SRC}/USBDevice/DeviceDriver/XInput/tud_xinput/tud_xinput.cpp
    ${SRC}/USBDevice/DeviceDriver/XboxOG/tud_xid/tud_xid.cpp

    ${SRC}/USBHost/GPIOHost/GPIOHost.cpp
    ${SRC}/USBHost/GPIOHost/psx_host/psx_host.c
    ${SRC}/USBHost/GPIOHost/joybus_host/joybus_controller.c
    ${SRC}/USBHost/GPIOHost/joybus_host/n64_host.c
    ${SRC}/USBHost/GPIOHost/dreamcast_host/MapleBus.cpp
    # libxsm3 for Xbox 360 (XSM3) authentication in XInput mode
    ${EXTERNAL_DIR}/libxsm3/xsm3.c
    ${EXTERNAL_DIR}/libxsm3/usbdsec.c
    ${EXTERNAL_DIR}/libxsm3/excrypt_des.c
    ${EXTERNAL_DIR}/libxsm3/excrypt_parve.c
    ${EXTERNAL_DIR}/libxsm3/excrypt_sha.c
)
set(LIBS_BOARD
    # Base
    pico_stdlib
    pico_multicore
    pico_rand
    pico_unique_id
    hardware_timer
    hardware_clocks
    hardware_flash
    tinyusb_device
    tinyusb_board
    # UART
    hardware_uart
    hardware_irq
    #fix16
    libfixmath
)
set(INC_DIRS_BOARD
)

# Config options

option(OGXM_SWITCH2_HID_RAW_LOG "UART: log Switch 2 Pro (0x2069) 64B HID when the 3 digital button bytes change only (Debug)" OFF)
option(OGXM_BT_CLEAR_KEYS_ON_BOOT "Debug: erase all Bluetooth bonds once at BT stack ready (do not leave on)" OFF)

set(MAX_GAMEPADS 1 CACHE STRING "Set number of gamepads, 1 to 4")
if (MAX_GAMEPADS GREATER 4 OR MAX_GAMEPADS LESS 1)
    message(FATAL_ERROR "MAX_GAMEPADS must be between 1 and 4")
endif()
add_definitions(-DMAX_GAMEPADS=${MAX_GAMEPADS})

set(OGXM_BOARD "PI_PICO" CACHE STRING "Set board type, options can be found in src/board_config.h")
set(FLASH_SIZE_MB 2)
set(PICO_BOARD none)

# Custom: the RP2040 + ESP32 hybrid's wires, movable at build time (hardware/README.md#pi-pico--esp32).
# Defaults: upstream's pinout, the USB host port on GP0 / GP1, and UART TX on GP28 with GP16 still
# carrying it. Stops on a pin that cannot do the job or is taken twice.
macro(ogxm_hybrid_pin_options)
    set(OGXM_HYBRID_I2C_SDA_PIN 18 CACHE STRING "Hybrid: I2C SDA (ESP32 GPIO21)")
    set(OGXM_HYBRID_I2C_SCL_PIN 19 CACHE STRING "Hybrid: I2C SCL (ESP32 GPIO22), same I2C as SDA")
    set(OGXM_HYBRID_UART_TX_PIN 28 CACHE STRING "Hybrid: UART0 TX (ESP32 RX0): 0, 12, 16 or 28")
    set(OGXM_HYBRID_UART_TX_ALT_PIN 16 CACHE STRING "Hybrid: second UART0 TX pin with the same signal, -1 = none")
    set(OGXM_HYBRID_UART_RX_PIN 17 CACHE STRING "Hybrid: UART0 RX (ESP32 TX0): 1, 13, 17 or 29")
    set(OGXM_HYBRID_ESP32_EN_PIN 8 CACHE STRING "Hybrid: ESP32 EN (restart)")
    set(OGXM_HYBRID_ESP32_IO0_PIN 20 CACHE STRING "Hybrid: ESP32 IO0 (bootloader)")
    set(OGXM_HYBRID_BRIDGE_JUMPER_PIN 21 CACHE STRING "Hybrid: jumper to GND at boot for the UART bridge")
    set(OGXM_HYBRID_USB_DP_PIN 0 CACHE STRING "Hybrid: USB host port D+ (D- is the next GPIO)")
    # A Pico W / Pico 2 W: GP25 is the radio chip's line (the hybrid keeps the radio off), not a LED.
    option(OGXM_HYBRID_PICO_W "Hybrid on a Pico W / Pico 2 W: no onboard status LED (GP25 left alone)" OFF)
endmacro()

function(ogxm_hybrid_pin_check)
    set(roles I2C_SDA I2C_SCL UART_TX UART_RX ESP32_EN ESP32_IO0 BRIDGE_JUMPER USB_DP)
    if(NOT OGXM_HYBRID_UART_TX_ALT_PIN EQUAL -1)
        list(APPEND roles UART_TX_ALT)
    endif()
    set(pins "")
    foreach(role IN LISTS roles)
        set(pin "${OGXM_HYBRID_${role}_PIN}")
        if(NOT pin MATCHES "^[0-9]+$" OR pin GREATER 29)
            message(FATAL_ERROR "OGXM_HYBRID_${role}_PIN=${pin}: expected a GPIO number")
        endif()
        list(APPEND pins "${pin}:${role}")
    endforeach()
    math(EXPR usb_dm "${OGXM_HYBRID_USB_DP_PIN} + 1")
    list(APPEND pins "${usb_dm}:USB_DM")
    if(DEFINED OGXM_EXT_RGB_PIN AND NOT OGXM_EXT_RGB_PIN EQUAL -1)
        list(APPEND pins "${OGXM_EXT_RGB_PIN}:EXT_RGB")
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND pins "4:DEBUG_UART_TX" "5:DEBUG_UART_RX")
    endif()
    # GP23 / 24 / 25 / 29: the Pico W radio, or a Pico's power and LED pins
    set(seen 23 24 25 29)
    set(seen_role_23 board)
    set(seen_role_24 board)
    set(seen_role_25 board)
    set(seen_role_29 board)
    foreach(entry IN LISTS pins)
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 pin)
        list(GET parts 1 role)
        if(pin IN_LIST seen)
            message(FATAL_ERROR "Hybrid pins: GP${pin} given to ${role} is already used by ${seen_role_${pin}}")
        endif()
        list(APPEND seen ${pin})
        set(seen_role_${pin} ${role})
    endforeach()
    # I2C: SDA on an even GPIO, SCL on an odd one, same controller (GPIO bit 1).
    math(EXPR sda_odd "${OGXM_HYBRID_I2C_SDA_PIN} % 2")
    math(EXPR scl_odd "${OGXM_HYBRID_I2C_SCL_PIN} % 2")
    math(EXPR sda_i2c "(${OGXM_HYBRID_I2C_SDA_PIN} >> 1) & 1")
    math(EXPR scl_i2c "(${OGXM_HYBRID_I2C_SCL_PIN} >> 1) & 1")
    if(NOT sda_odd EQUAL 0 OR NOT scl_odd EQUAL 1 OR NOT sda_i2c EQUAL scl_i2c)
        message(FATAL_ERROR "Hybrid pins: GP${OGXM_HYBRID_I2C_SDA_PIN} / GP${OGXM_HYBRID_I2C_SCL_PIN} are not SDA / SCL of one I2C (e.g. 18 / 19, 26 / 27, 6 / 7)")
    endif()
    # UART0 only (the firmware talks to the ESP32 on uart0)
    set(uart0_tx 0 12 16 28)
    set(uart0_rx 1 13 17 29)
    foreach(role UART_TX UART_TX_ALT)
        if(role IN_LIST roles AND NOT OGXM_HYBRID_${role}_PIN IN_LIST uart0_tx)
            message(FATAL_ERROR "OGXM_HYBRID_${role}_PIN=${OGXM_HYBRID_${role}_PIN}: UART0 TX is GP0, 12, 16 or 28")
        endif()
    endforeach()
    if(NOT OGXM_HYBRID_UART_RX_PIN IN_LIST uart0_rx)
        message(FATAL_ERROR "OGXM_HYBRID_UART_RX_PIN=${OGXM_HYBRID_UART_RX_PIN}: UART0 RX is GP1, 13, 17 or 29")
    endif()
    add_compile_definitions(
        OGXM_HYBRID_I2C_SDA_PIN=${OGXM_HYBRID_I2C_SDA_PIN}
        OGXM_HYBRID_I2C_SCL_PIN=${OGXM_HYBRID_I2C_SCL_PIN}
        OGXM_HYBRID_UART_TX_PIN=${OGXM_HYBRID_UART_TX_PIN}
        OGXM_HYBRID_UART_TX_ALT_PIN=${OGXM_HYBRID_UART_TX_ALT_PIN}
        OGXM_HYBRID_UART_RX_PIN=${OGXM_HYBRID_UART_RX_PIN}
        OGXM_HYBRID_ESP32_EN_PIN=${OGXM_HYBRID_ESP32_EN_PIN}
        OGXM_HYBRID_ESP32_IO0_PIN=${OGXM_HYBRID_ESP32_IO0_PIN}
        OGXM_HYBRID_BRIDGE_JUMPER_PIN=${OGXM_HYBRID_BRIDGE_JUMPER_PIN}
        OGXM_HYBRID_USB_DP_PIN=${OGXM_HYBRID_USB_DP_PIN}
    )
    if(OGXM_HYBRID_PICO_W)
        add_compile_definitions(OGXM_HYBRID_PICO_W=1)
    endif()
    message(STATUS "Hybrid pins: I2C ${OGXM_HYBRID_I2C_SDA_PIN}/${OGXM_HYBRID_I2C_SCL_PIN}, UART TX ${OGXM_HYBRID_UART_TX_PIN} (also ${OGXM_HYBRID_UART_TX_ALT_PIN}) RX ${OGXM_HYBRID_UART_RX_PIN}, EN ${OGXM_HYBRID_ESP32_EN_PIN}, IO0 ${OGXM_HYBRID_ESP32_IO0_PIN}, bridge jumper ${OGXM_HYBRID_BRIDGE_JUMPER_PIN}, USB host ${OGXM_HYBRID_USB_DP_PIN}/${usb_dm}")
endfunction()

# Custom: external WS2812 status LED on a Pico 2 / Pico 2 W board (see hardware/README.md).
macro(ogxm_ext_rgb_options)
    set(OGXM_EXT_RGB_PIN 6 CACHE STRING "Pico 2 W: GPIO of an external WS2812 status LED, -1 = none")
    set(OGXM_EXT_RGB_COUNT 1 CACHE STRING "Pico 2 W: WS2812 LEDs chained on that pin, 1 to 4")
    set(OGXM_EXT_RGB_BRIGHTNESS 48 CACHE STRING "Pico 2 W: WS2812 status LED brightness, 1 to 255")
    if(NOT OGXM_EXT_RGB_PIN EQUAL -1)
        if(OGXM_EXT_RGB_COUNT LESS 1 OR OGXM_EXT_RGB_COUNT GREATER 4)
            message(FATAL_ERROR "OGXM_EXT_RGB_COUNT must be between 1 and 4")
        endif()
        if(OGXM_EXT_RGB_BRIGHTNESS LESS 1 OR OGXM_EXT_RGB_BRIGHTNESS GREATER 255)
            message(FATAL_ERROR "OGXM_EXT_RGB_BRIGHTNESS must be between 1 and 255")
        endif()
        set(EN_RGB TRUE)
        add_compile_definitions(
            OGXM_EXT_RGB_PIN=${OGXM_EXT_RGB_PIN}
            OGXM_EXT_RGB_COUNT=${OGXM_EXT_RGB_COUNT}
            OGXM_EXT_RGB_BRIGHTNESS=${OGXM_EXT_RGB_BRIGHTNESS}
        )
    endif()
endmacro()

if (OGXM_BOARD STREQUAL "PI_PICO")
    add_compile_definitions(CONFIG_OGXM_BOARD_PI_PICO=1)
    set(EN_USB_HOST TRUE)

elseif (OGXM_BOARD STREQUAL "PI_PICO2")
    add_compile_definitions(CONFIG_OGXM_BOARD_PI_PICO2=1)
    set(EN_USB_HOST TRUE)
    set(PICO_BOARD pico2)
    set(PICO_PLATFORM rp2350)
    set(FLASH_SIZE_MB 4)

elseif(OGXM_BOARD STREQUAL "PI_PICOW")
    add_compile_definitions(CONFIG_OGXM_BOARD_PI_PICOW=1)
    set(EN_BLUETOOTH TRUE)
    set(EN_USB_HOST TRUE)  # PIO USB host for Wii mode (controller on external USB)
    set(PICO_BOARD pico_w)

elseif(OGXM_BOARD STREQUAL "PI_PICO2W")
    add_compile_definitions(CONFIG_OGXM_BOARD_PI_PICO2W=1)
    set(EN_BLUETOOTH TRUE)
    set(EN_USB_HOST TRUE)  # PIO USB host for Wii mode (controller on external USB)
    set(PICO_BOARD pico2_w)
    set(PICO_PLATFORM rp2350)
    set(FLASH_SIZE_MB 4)
    ogxm_ext_rgb_options()  # Custom: external WS2812 status LED

elseif(OGXM_BOARD STREQUAL "ADAFRUIT_FEATHER")
    add_compile_definitions(CONFIG_OGXM_BOARD_ADAFRUIT_FEATHER=1)
    set(EN_USB_HOST TRUE)
    set(EN_RGB TRUE)
    set(FLASH_SIZE_MB 8)

elseif(OGXM_BOARD STREQUAL "RP2040_ZERO")
    add_compile_definitions(CONFIG_OGXM_BOARD_RP2040_ZERO=1)
    set(EN_USB_HOST TRUE)
    set(EN_RGB TRUE)

# elseif(OGXM_BOARD STREQUAL "INTERNAL_4CH")
#     set(EN_USB_HOST TRUE)
#     set(EN_4CH TRUE)

elseif(OGXM_BOARD STREQUAL "EXTERNAL_4CH_I2C")
    add_compile_definitions(CONFIG_OGXM_BOARD_EXTERNAL_4CH=1)
    set(EN_USB_HOST TRUE)
    set(EN_4CH TRUE)
    set(EN_RGB TRUE)

elseif(OGXM_BOARD STREQUAL "ESP32_BLUEPAD32_I2C" OR OGXM_BOARD STREQUAL "ESP32_BLUEPAD32_I2C_PICO2")
    add_compile_definitions(CONFIG_OGXM_BOARD_ESP32_BLUEPAD32_I2C=1)
    set(EN_ESP32 TRUE)
    set(EN_UART_BRIDGE TRUE)
    set(EN_USB_HOST TRUE)  # Custom: USB host port on GP0 / GP1 (wired controllers, DS3 pairing)
    ogxm_hybrid_pin_options()
    # Custom: the same hybrid on a Pico 2 / Pico 2 W (RP2350, Pico pinout, 4 MB flash). The
    # Pico 2 W radio is not used: Bluetooth comes from the ESP32, and the pins wired to the
    # CYW43 (GP23 / 24 / 25 / 29) are not touched.
    if(OGXM_BOARD STREQUAL "ESP32_BLUEPAD32_I2C_PICO2")
        set(PICO_BOARD pico2)
        set(PICO_PLATFORM rp2350)
        set(FLASH_SIZE_MB 4)
        ogxm_ext_rgb_options()  # Custom: external WS2812 status LED (PIO block 2 is free here)
    endif()
    ogxm_hybrid_pin_check()

    if(OGXM_RETAIL STREQUAL "TRUE")
        message(STATUS "Retail mode enabled.")
        add_compile_definitions(OGXM_RETAIL=1)
    endif()

elseif(OGXM_BOARD STREQUAL "ESP32_BLUERETRO_I2C")
    add_compile_definitions(CONFIG_OGXM_BOARD_ESP32_BLUERETRO_I2C=1)
    set(EN_ESP32 TRUE)
    set(EN_BLUERETRO_I2C TRUE)
    set(EN_UART_BRIDGE TRUE)

    if(OGXM_RETAIL STREQUAL "TRUE")
        message(STATUS "Retail mode enabled.")
        add_compile_definitions(OGXM_RETAIL=1)
    endif()

elseif(OGXM_BOARD STREQUAL "RP2350_USB_A")
    add_compile_definitions(CONFIG_OGXM_BOARD_RP2350_USB_A=1)
    set(EN_USB_HOST TRUE)
    set(PICO_PLATFORM rp2350)
    set(EN_RGB TRUE)
    set(FLASH_SIZE_MB 2)

elseif(OGXM_BOARD STREQUAL "RP2350_ZERO")
    add_compile_definitions(CONFIG_OGXM_BOARD_RP2350_ZERO=1)
    set(EN_USB_HOST TRUE)
    set(PICO_PLATFORM rp2350)
    set(EN_RGB TRUE)
    set(FLASH_SIZE_MB 2)

elseif(OGXM_BOARD STREQUAL "RP2040_XIAO")
    add_compile_definitions(CONFIG_OGXM_BOARD_RP2040_XIAO=1)
    set(EN_USB_HOST TRUE)
    set(EN_RGB TRUE)

elseif(OGXM_BOARD STREQUAL "RP2354")
    add_compile_definitions(CONFIG_OGXM_BOARD_RP2354=1)
    set(EN_BLUETOOTH TRUE)
    set(EN_USB_HOST TRUE)
    set(PICO_BOARD pico2_w)
    set(PICO_PLATFORM rp2350)
    set(FLASH_SIZE_MB 4)

elseif(OGXM_BOARD STREQUAL "OGX_LIVE")
    # nightowl3090/OGX-LIVE: bare RP2040, W25Q128 16MB flash, PIO USB host GP10/11 (USB-A)
    add_compile_definitions(CONFIG_OGXM_BOARD_OGX_LIVE=1)
    set(EN_USB_HOST TRUE)
    set(FLASH_SIZE_MB 16)

else()
    message(FATAL_ERROR "Invalid OGXM_BOARD value. See options in src/Board/Config.h")

endif()

# Optional: default or lock USB output mode.
# OGXM_FIXED_DRIVER = one of: XINPUT, DINPUT, XBOXOG, XBOXOG_SB, XBOXOG_XR, SWITCH, WIIU, WII, PS3, PSCLASSIC, WEBAPP, PS1PS2, GAMECUBE, DREAMCAST, N64, PS4, STEAM.
#   Empty = use first valid driver as default, combos enabled.
#   Set = use that mode as default (first boot / NVS init). If OGXM_FIXED_DRIVER_ALLOW_COMBOS=OFF (default):
#     combos disabled, mode locked. If OGXM_FIXED_DRIVER_ALLOW_COMBOS=ON: default is this mode, combos still work.
set(OGXM_FIXED_DRIVER "" CACHE STRING "Default/lock output mode. One of: XINPUT, DINPUT, XBOXOG, XBOXOG_SB, XBOXOG_XR, SWITCH, WIIU, WII, PS3, PSCLASSIC, WEBAPP, PS1PS2, GAMECUBE, DREAMCAST, N64, PS4, STEAM. Empty = combos enabled.")
set(OGXM_FIXED_DRIVER_ALLOW_COMBOS OFF CACHE BOOL "If OGXM_FIXED_DRIVER is set: ON = use it as default but allow combos to change mode; OFF = lock to that mode, combos disabled.")
if(OGXM_FIXED_DRIVER)
    add_compile_definitions(CONFIG_OGXM_FIXED_DRIVER=1)
    # WII is build-only: no combos to switch into or out of Wii.
    if(OGXM_FIXED_DRIVER_ALLOW_COMBOS AND NOT (OGXM_FIXED_DRIVER STREQUAL "WII"))
        add_compile_definitions(CONFIG_OGXM_FIXED_DRIVER_ALLOW_COMBOS=1)
    endif()
    if(OGXM_FIXED_DRIVER STREQUAL "XBOXOG")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=1)
    elseif(OGXM_FIXED_DRIVER STREQUAL "XBOXOG_SB")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=2)
    elseif(OGXM_FIXED_DRIVER STREQUAL "XBOXOG_XR")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=3)
    elseif(OGXM_FIXED_DRIVER STREQUAL "XINPUT")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=4)
    elseif(OGXM_FIXED_DRIVER STREQUAL "PS3")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=5)
    elseif(OGXM_FIXED_DRIVER STREQUAL "DINPUT")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=6)
    elseif(OGXM_FIXED_DRIVER STREQUAL "PSCLASSIC")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=7)
    elseif(OGXM_FIXED_DRIVER STREQUAL "SWITCH")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=8)
    elseif(OGXM_FIXED_DRIVER STREQUAL "WIIU")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=9)
    elseif(OGXM_FIXED_DRIVER STREQUAL "WII")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=10)
    elseif(OGXM_FIXED_DRIVER STREQUAL "PS1PS2")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=11)
    elseif(OGXM_FIXED_DRIVER STREQUAL "GAMECUBE")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=12)
    elseif(OGXM_FIXED_DRIVER STREQUAL "DREAMCAST")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=13)
    elseif(OGXM_FIXED_DRIVER STREQUAL "N64")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=14)
    elseif(OGXM_FIXED_DRIVER STREQUAL "PS4")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=15)
    elseif(OGXM_FIXED_DRIVER STREQUAL "STEAM")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=16)
    elseif(OGXM_FIXED_DRIVER STREQUAL "WEBAPP")
        add_compile_definitions(OGXM_FIXED_DRIVER_TYPE=100)
    else()
        message(FATAL_ERROR "OGXM_FIXED_DRIVER must be one of: XINPUT, DINPUT, XBOXOG, XBOXOG_SB, XBOXOG_XR, SWITCH, WIIU, WII, PS3, PSCLASSIC, WEBAPP, PS1PS2, GAMECUBE, DREAMCAST, N64, PS4, STEAM")
    endif()
    if(OGXM_FIXED_DRIVER STREQUAL "WII")
        message(STATUS "Wii-only build: ${OGXM_FIXED_DRIVER} (combos disabled, build option only)")
    elseif(OGXM_FIXED_DRIVER_ALLOW_COMBOS)
        message(STATUS "Default output mode: ${OGXM_FIXED_DRIVER} (combos enabled)")
    else()
        message(STATUS "Fixed output mode: ${OGXM_FIXED_DRIVER} (combos disabled)")
    endif()
endif()

# Do not add -DOGXM_BOARD here; Config.h defines OGXM_BOARD from CONFIG_OGXM_BOARD_* to avoid redefinition warnings

# Main loop delay (microseconds). 0 = lowest latency (default). Set to 250+ for lower CPU.
set(MAIN_LOOP_DELAY_US 0 CACHE STRING "Main loop delay in microseconds; 0 = low latency (default)")
add_compile_definitions(MAIN_LOOP_DELAY_US=${MAIN_LOOP_DELAY_US})

# Custom (OGX-Mini-improved): defaults of the dongle options (UserSettings/DongleSettings). Every one
# can be changed at runtime from the web app (Adapter Options); these only set the defaults.
# Disconnect every Bluetooth pad before a mode-change reboot: pads turn off (LED off, a visible
# sign the mode changed) and reconnect on a button press; Joy-Cons no longer hang with the LED on.
# Off in published builds (the upstream behaviour), on in our own (OGXM_DEV_DEFAULTS).
option(OGXM_DISCONNECT_PADS_ON_MODE_CHANGE "Default: disconnect Bluetooth pads before a mode-change reboot" ${OGXM_DEV_DEFAULTS})

# Joy-Con motion defaults (dongle options).
set(OGXM_JOYCON_PAIR_IMU_SIDE "RIGHT" CACHE STRING "Joy-Con half providing motion in a merged pair (RIGHT|LEFT)")
set(OGXM_JOYCON_PAIR_ORIENTATION "VERTICAL" CACHE STRING "Merged Joy-Con pair orientation (VERTICAL|HORIZONTAL)")
set(OGXM_JOYCON_SOLO_ORIENTATION "HORIZONTAL" CACHE STRING "Single Joy-Con orientation (VERTICAL|HORIZONTAL)")
set(OGXM_JOYCON_PAIR_RUMBLE "PER_SIDE" CACHE STRING "Merged Joy-Con pair rumble: left motor on the left Joy-Con and right on the right one, as SDL / Steam do (PER_SIDE), or both motors on both (BOTH)")
set_property(CACHE OGXM_JOYCON_PAIR_IMU_SIDE PROPERTY STRINGS RIGHT LEFT)
set_property(CACHE OGXM_JOYCON_PAIR_ORIENTATION PROPERTY STRINGS VERTICAL HORIZONTAL)
set_property(CACHE OGXM_JOYCON_SOLO_ORIENTATION PROPERTY STRINGS VERTICAL HORIZONTAL)
set_property(CACHE OGXM_JOYCON_PAIR_RUMBLE PROPERTY STRINGS PER_SIDE BOTH)
foreach(_opt IN ITEMS OGXM_JOYCON_PAIR_IMU_SIDE:RIGHT:LEFT OGXM_JOYCON_PAIR_ORIENTATION:HORIZONTAL:VERTICAL
                      OGXM_JOYCON_SOLO_ORIENTATION:HORIZONTAL:VERTICAL OGXM_JOYCON_PAIR_RUMBLE:PER_SIDE:BOTH)
    string(REPLACE ":" ";" _parts "${_opt}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _true_value)
    list(GET _parts 2 _false_value)
    if (NOT ${_name} STREQUAL _true_value AND NOT ${_name} STREQUAL _false_value)
        message(FATAL_ERROR "${_name} must be ${_true_value} or ${_false_value}, got '${${_name}}'")
    endif()
endforeach()
if (OGXM_JOYCON_PAIR_RUMBLE STREQUAL "PER_SIDE")
    add_compile_definitions(OGXM_JOYCON_PAIR_RUMBLE_PER_SIDE=1)
else()
    add_compile_definitions(OGXM_JOYCON_PAIR_RUMBLE_PER_SIDE=0)
endif()
if (OGXM_JOYCON_PAIR_IMU_SIDE STREQUAL "RIGHT")
    add_compile_definitions(OGXM_JOYCON_PAIR_IMU_RIGHT=1)
else()
    add_compile_definitions(OGXM_JOYCON_PAIR_IMU_RIGHT=0)
endif()
if (OGXM_JOYCON_PAIR_ORIENTATION STREQUAL "HORIZONTAL")
    add_compile_definitions(OGXM_JOYCON_PAIR_HORIZONTAL=1)
else()
    add_compile_definitions(OGXM_JOYCON_PAIR_HORIZONTAL=0)
endif()
if (OGXM_JOYCON_SOLO_ORIENTATION STREQUAL "HORIZONTAL")
    add_compile_definitions(OGXM_JOYCON_SOLO_HORIZONTAL=1)
else()
    add_compile_definitions(OGXM_JOYCON_SOLO_HORIZONTAL=0)
endif()
message(STATUS "Joy-Con motion: pair IMU ${OGXM_JOYCON_PAIR_IMU_SIDE}, pair ${OGXM_JOYCON_PAIR_ORIENTATION}, solo ${OGXM_JOYCON_SOLO_ORIENTATION}; pair rumble ${OGXM_JOYCON_PAIR_RUMBLE}")
if(OGXM_DISCONNECT_PADS_ON_MODE_CHANGE)
    add_compile_definitions(OGXM_DISCONNECT_PADS_ON_MODE_CHANGE=1)
    message(STATUS "Pad disconnect on mode change: enabled")
endif()

# More dongle option defaults.
option(OGXM_MAC_PER_CONTROLLER "Default: PS4/STEAM modes report the connected pad's Bluetooth address instead of the dongle's" OFF)
option(OGXM_PS4_LEGACY_MOTION_SCALE "Default: PS4 mode uses the legacy Brook-style motion scale (gyro/8, accel/64) instead of real DS4 units" OFF)
option(OGXM_SINGLE_CONTROLLER "Default: no search for new controllers once one is connected (both search times 0)" OFF)
if(OGXM_MAC_PER_CONTROLLER)
    add_compile_definitions(OGXM_MAC_PER_CONTROLLER=1)
endif()
if(OGXM_PS4_LEGACY_MOTION_SCALE)
    add_compile_definitions(OGXM_PS4_LEGACY_MOTION_SCALE=1)
endif()
if(OGXM_SINGLE_CONTROLLER)
    add_compile_definitions(OGXM_SINGLE_CONTROLLER=1)
endif()
# Defaults of the dongle options with a value (all changeable from the web app).
set(OGXM_FULL_SEARCH_S "60" CACHE STRING "Default: full search for new controllers while a slot is open, in seconds (0-600)")
set(OGXM_REDUCED_SEARCH_S "NO_LIMIT" CACHE STRING "Default: reduced search after the full one, in seconds (0-600) or NO_LIMIT")
set(OGXM_COMBO_DISABLED_MODES "0" CACHE STRING "Default: output modes whose button combo is off, bit mask by DeviceDriverType (e.g. 0x8000 = PS4)")
# Keep the log in flash too (Board/FlashLog.h), so it outlives unplugging the adapter: off in
# published builds, on in our own (OGXM_DEV_DEFAULTS).
option(OGXM_FLASH_LOG "Default: keep the log in flash too" ${OGXM_DEV_DEFAULTS})
if(OGXM_FLASH_LOG)
    add_compile_definitions(OGXM_FLASH_LOG=1)
endif()
# Detailed link diagnostics (Bluepad32/LinkDiag link queries: RSSI, channels, failed contacts,
# transmit power, the controller's chip): off in published builds, on in our own.
option(OGXM_DIAGNOSTICS_LINK "Default: detailed Bluetooth link diagnostics" ${OGXM_DEV_DEFAULTS})
if(OGXM_DIAGNOSTICS_LINK)
    add_compile_definitions(OGXM_DIAGNOSTICS_LINK=1)
endif()
option(OGXM_HIGH_TX_POWER "Default: higher Bluetooth transmit power where the radio allows it (hybrid: +9 dBm)" OFF)
if(OGXM_HIGH_TX_POWER)
    add_compile_definitions(OGXM_HIGH_TX_POWER=1)
endif()
set(OGXM_IDLE_OFF_MINUTES "15" CACHE STRING "Default: turn a Bluetooth controller off after this many minutes without input (0 = never, 1-255)")
if (OGXM_REDUCED_SEARCH_S STREQUAL "NO_LIMIT")
    set(_ogxm_reduced_search 0xFFF)
else()
    set(_ogxm_reduced_search ${OGXM_REDUCED_SEARCH_S})
endif()
add_compile_definitions(OGXM_FULL_SEARCH_S=${OGXM_FULL_SEARCH_S} OGXM_REDUCED_SEARCH_S=${_ogxm_reduced_search}
                        OGXM_COMBO_DISABLED_MODES=${OGXM_COMBO_DISABLED_MODES} OGXM_IDLE_OFF_MINUTES=${OGXM_IDLE_OFF_MINUTES})
message(STATUS "Dongle defaults: search ${OGXM_FULL_SEARCH_S} s full / ${OGXM_REDUCED_SEARCH_S} reduced, combos off ${OGXM_COMBO_DISABLED_MODES}, idle off ${OGXM_IDLE_OFF_MINUTES} min")

if(EN_USB_HOST)
    message(STATUS "USB host enabled.")
    add_compile_definitions(CONFIG_EN_USB_HOST=1)
    list(APPEND SOURCES_BOARD
        ${SRC}/USBHost/tuh_callbacks.cpp
        ${SRC}/USBHost/PioHostPort.cpp
        ${SRC}/USBHost/PioUsbRelease.c
        # HID
        ${SRC}/USBHost/HostDriver/DInput/DInput.cpp
        ${SRC}/USBHost/HostDriver/PSClassic/PSClassic.cpp
        ${SRC}/USBHost/HostDriver/SwitchWired/SwitchWired.cpp
        ${SRC}/USBHost/HostDriver/SwitchPro/SwitchPro.cpp
        ${SRC}/USBHost/HostDriver/SwitchPro/Switch2ProHost.cpp
        ${SRC}/USBHost/HostDriver/PS5/PS5.cpp
        ${SRC}/USBHost/HostDriver/PS4/PS4.cpp
        ${SRC}/USBHost/HostDriver/PS3/PS3.cpp
        ${SRC}/USBHost/HostDriver/N64/N64.cpp
        ${SRC}/USBHost/HostDriver/HIDGeneric/HIDGeneric.cpp
        ${SRC}/USBHost/HostDriver/FlydigiApex4Wukong/FlydigiApex4Wukong.cpp
        ${SRC}/USBHost/HostDriver/GameSirCyclone2/GameSirCyclone2.cpp
        ${SRC}/USBHost/HostDriver/GameSirCyclone2/GameSirCyclone2Trace.cpp
        ${SRC}/USBHost/HostDriver/GameSirCyclone2/Cyclone2SwitchWired.cpp
        ${SRC}/USBHost/HostDriver/GameSirCyclone2/Cyclone2Ds4Wired.cpp
        ${SRC}/USBHost/HostDriver/GameSirCyclone2/Cyclone2HidPassive.cpp
        ${SRC}/USBHost/HostDriver/GameSirG7Pro/GameSirG7Pro.cpp

        ${SRC}/USBHost/HIDParser/HIDJoystick.cpp
        ${SRC}/USBHost/HIDParser/HIDReportDescriptor.cpp
        ${SRC}/USBHost/HIDParser/HIDReportDescriptorElements.cpp
        ${SRC}/USBHost/HIDParser/HIDReportDescriptorUsages.cpp
        ${SRC}/USBHost/HIDParser/HIDUtils.cpp

        # XInput
        ${SRC}/USBHost/HostDriver/XInput/XboxOG.cpp
        ${SRC}/USBHost/HostDriver/XInput/XboxOne.cpp
        ${SRC}/USBHost/HostDriver/XInput/Xbox360.cpp
        ${SRC}/USBHost/HostDriver/XInput/Xbox360W.cpp
        ${SRC}/USBHost/HostDriver/XInput/tuh_xinput/tuh_xinput.cpp
        ${SRC}/USBHost/HostDriver/VictrixGambit/VictrixGambit.cpp
    )
    list(APPEND LIBS_BOARD
        tinyusb_host
        tinyusb_pico_pio_usb
    )
endif()

if(EN_BLUETOOTH)
    add_compile_definitions(CONFIG_EN_BLUETOOTH=1)
    message(STATUS "Bluetooth enabled.")

    generate_gatt_header(${BTSTACK_ROOT} ${COMMON}/Bluepad32/att_delayed_response.gatt ${SRC}/BLEServer/att_delayed_response.h)
    
    list(APPEND SOURCES_BOARD
        ${SRC}/Bluepad32/Bluepad32.cpp
        ${COMMON}/Bluepad32/ScanControl.cpp
        ${COMMON}/Bluepad32/LinkDiag.cpp
        ${SRC}/Bluepad32/ClassicPairingDebug.cpp
        ${COMMON}/Bluepad32/JoyConSettings.cpp
        ${COMMON}/Bluepad32/BLEServer.cpp
        ${SRC}/BLEServer/BleStore.cpp
        ${SRC}/USBHost/HostDriver/FlydigiApex4Wukong/FlydigiApex4WukongBtProbe.cpp
        ${SRC}/USBHost/HostDriver/FlydigiApex4Wukong/FlydigiApex4WukongBt.cpp
        ${SRC}/USBHost/HostDriver/FlydigiApex4Wukong/FlydigiApex4WukongBtL2cap.cpp
        ${SRC}/USBHost/HostDriver/GameSirCyclone2/Cyclone2BtProbe.cpp
        ${SRC}/USBHost/HostDriver/GameSirG7Pro/GameSirG7ProBt.cpp
    )
    list(APPEND INC_DIRS_BOARD
        ${SRC}
        ${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include
        ${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include
    )
    list(APPEND LIBS_BOARD
        pico_cyw43_arch_none
        pico_btstack_classic
        pico_btstack_cyw43
        bluepad32
    )

    # Wii mode: Wiimote BT emulator (Pico W/2W with USB host)
    if(EN_USB_HOST AND (OGXM_BOARD STREQUAL "PI_PICOW" OR OGXM_BOARD STREQUAL "PI_PICO2W" OR OGXM_BOARD STREQUAL "RP2354"))
        message(STATUS "Wii Wiimote lib enabled.")
        list(APPEND SOURCES_BOARD
            ${SRC}/Wii/utils.c
            ${SRC}/Wii/WiiReportConverter.cpp
            ${SRC}/Wii/wiimote-lib/wiimote_btstack.c
            ${SRC}/Wii/wiimote-lib/wiimote.c
            ${SRC}/Wii/wiimote-lib/wm_reports.c
            ${SRC}/Wii/wiimote-lib/motion.c
            ${SRC}/Wii/wiimote-lib/wm_crypto.c
            ${SRC}/Wii/wiimote-lib/inet.c
        )
        list(APPEND INC_DIRS_BOARD
            ${SRC}/Wii
            ${SRC}/Wii/wiimote-lib
        )
    endif()
endif()

if(EN_RGB)
    add_compile_definitions(CONFIG_EN_RGB=1)
    message(STATUS "RGB enabled.")
    list(APPEND SOURCES_BOARD
        ${SRC}/Board/Pico_WS2812/WS2812.cpp
    )
    list(APPEND LIBS_BOARD
        hardware_pio
    )
endif()

if(EN_4CH)
    add_compile_definitions(CONFIG_EN_4CH=1)
    message(STATUS "4CH enabled.")
    # list(APPEND SOURCES_BOARD
    #     ${SRC}/I2CDriver/4Channel/I2CMaster.cpp
    #     ${SRC}/I2CDriver/4Channel/I2CSlave.cpp
    # )
    list(APPEND LIBS_BOARD
        hardware_i2c
        pico_i2c_slave
    )
endif()

if(EN_ESP32)
    add_compile_definitions(CONFIG_EN_ESP32=1)
    message(STATUS "ESP32 enabled.")
    if (EN_BLUERETRO_I2C)
        # Nothing
    else()
        list(APPEND LIBS_BOARD
            pico_i2c_slave
        )
        # Joy-Con motion orientation for pads the ESP32 forwards (Firmware/common/Bluepad32)
        list(APPEND SOURCES_BOARD
            ${COMMON}/Bluepad32/JoyConSettings.cpp
        )
    endif()
    list(APPEND LIBS_BOARD
        hardware_i2c
        hardware_pio
        hardware_dma
    )
endif()

if(EN_UART_BRIDGE)
    add_compile_definitions(CONFIG_EN_UART_BRIDGE=1)
    message(STATUS "UART bridge enabled.")
    list(APPEND SOURCES_BOARD
        ${SRC}/USBDevice/DeviceDriver/UARTBridge/UARTBridge.cpp
        ${SRC}/USBDevice/DeviceDriver/UARTBridge/uart_bridge/uart_bridge.c
    )
endif()

string(TIMESTAMP CURRENT_DATETIME "%Y-%m-%d %H:%M:%S")
add_compile_definitions(BUILD_DATETIME="${CURRENT_DATETIME}")
add_compile_definitions(FIRMWARE_NAME="${FW_NAME}")
add_compile_definitions(FIRMWARE_VERSION="${FW_VERSION}" OGXM_VERSION="${OGXM_VERSION}" OGXM_BUILD_ID="${OGXM_BUILD_ID}")
add_compile_definitions(OGXM_BOARD_NAME="${OGXM_BOARD}")
# Custom: panic() records the message for the diagnostics and reboots (Diagnostics/CrashHandler.cpp).
add_compile_definitions(PICO_PANIC_FUNCTION=ogxm_panic)
add_compile_definitions(PICO_FLASH_SIZE_BYTES=${FLASH_SIZE_MB}*1024*1024)
add_compile_definitions(NVS_SECTORS=4)

# Check for DVD dongle firmware
if(EXISTS ${SRC}/USBDevice/DeviceDriver/XboxOG/tud_xid/tud_xid_xremote_rom.h)
    message(STATUS "XRemote ROM available.")
    add_definitions(-DXREMOTE_ROM_AVAILABLE)
endif()

# RP2040-only: force generic boot2 when no Bluetooth (RP2350 boards use platform boot2).
# Decided by the platform each board sets above, so a new RP2350 board needs no entry here.
if(NOT EN_BLUETOOTH AND NOT (PICO_PLATFORM MATCHES "^rp2350"))
    add_compile_definitions(PICO_BOOT_STAGE2_CHOOSE_GENERIC_03H=1)
    add_compile_definitions(PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)
endif()

# Cross-compile: skip linker in compiler check (ARM linker does not accept Windows flags)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_SYSTEM_NAME PICO)
set(CMAKE_CXX_STANDARD_LIBRARIES "")
set(CMAKE_C_STANDARD_LIBRARIES "")

# Use our Platform/PICO.cmake so CMake does not add Windows linker flags (--out-implib, etc.)
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(${FW_NAME} C CXX ASM)

pico_sdk_init()

# Custom: Pico SDK 2.3.1 (Firmware/cmake/patch_libs.cmake). An older SDK builds but brings back
# Bluetooth timers that wait for the next controller packet (pico-sdk #2838, #2127).
if (PICO_SDK_VERSION_STRING VERSION_LESS "2.3.0")
    message(FATAL_ERROR "Pico SDK ${PICO_SDK_VERSION_STRING} found at ${PICO_SDK_PATH}; this firmware needs 2.3.1 "
                        "(Firmware/RP2040/docs/Building_From_Source.md)")
endif ()

add_executable(${FW_NAME} ${SOURCES_BOARD})

set(BUILD_STR "")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build enabled (UART logging at 115200; TinyUSB host prevents USB CDC stdio — use UART TX pin, see CMakeLists UART message).")
    set(BUILD_STR "-Debug")

    # Pico W/2W: use UART1 on GP4 (TX) / GP5 (RX) - valid UART pins, avoids PIO USB (GP0/1) in Wii mode
    # Note: GP10/11 are not valid UART pins on RP2040/RP2350
    # The RP2040 + ESP32 hybrid too: GP0 / GP1 are its USB host port (wired controllers)
    if(OGXM_BOARD STREQUAL "PI_PICOW" OR OGXM_BOARD STREQUAL "PI_PICO2W" OR OGXM_BOARD STREQUAL "RP2354" OR
       OGXM_BOARD STREQUAL "ESP32_BLUEPAD32_I2C" OR OGXM_BOARD STREQUAL "ESP32_BLUEPAD32_I2C_PICO2")
        set(TX_PIN 4)
        set(RX_PIN 5)
    else()
        set(TX_PIN 0)
        set(RX_PIN 1)
    endif()
    set(UART_PORT)

    include(${CMAKE_CURRENT_LIST_DIR}/cmake/get_pico_uart_port.cmake)
    get_pico_uart_port(${TX_PIN} UART_PORT)

    message(STATUS "UART port: ${UART_PORT}, TX: ${TX_PIN}, RX: ${RX_PIN}")

    # USB CDC stdio is disabled by Pico SDK when TinyUSB host is linked; OGXM_LOG uses printf → UART only.
    pico_enable_stdio_usb(${FW_NAME} 0)
    pico_enable_stdio_uart(${FW_NAME} 1)
    target_compile_definitions(${FW_NAME} PRIVATE
        PICO_DEFAULT_UART=${UART_PORT}
        PICO_DEFAULT_UART_TX_PIN=${TX_PIN}
        PICO_DEFAULT_UART_RX_PIN=${RX_PIN}
    )
 
    add_compile_definitions(CFG_TUSB_DEBUG=1)
    add_compile_definitions(OGXM_DEBUG=1)
    add_compile_definitions(CONFIG_OGXM_DEBUG=1)

    if(OGXM_SWITCH2_HID_RAW_LOG)
        target_compile_definitions(${FW_NAME} PRIVATE OGXM_SWITCH2_HID_RAW_LOG=1)
        message(STATUS "OGXM_SWITCH2_HID_RAW_LOG: UART hex when Switch 2 Pro 3 digital button bytes change")
    endif()
    if(OGXM_BT_CLEAR_KEYS_ON_BOOT)
        target_compile_definitions(${FW_NAME} PRIVATE OGXM_BT_CLEAR_KEYS_ON_BOOT=1)
        message(STATUS "OGXM_BT_CLEAR_KEYS_ON_BOOT: will erase Bluetooth bonds at stack ready (Debug diagnostic)")
    endif()

    target_compile_options(${FW_NAME} PRIVATE
        -Wall                # Enable most warnings
        -Wextra              # Enable extra warnings
        -Wconversion         # Warn on type conversion issues
        -Wsign-conversion    # Warn on sign conversion issues
        -Wno-unused-parameter # Disable warnings for unused parameters
        # -Werror              # Treat warnings as errors
    )

elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    pico_enable_stdio_uart(${FW_NAME} 0)
    add_compile_definitions(CFG_TUSB_DEBUG=0)
    add_compile_options(
        -O3                     # Optimize for speed
        # -mcpu=cortex-m0plus     # Target ARM Cortex-M0+
        -mthumb                 # Use Thumb instruction set
        -ffunction-sections     # Place each function in its own section
        -fdata-sections         # Place each data item in its own section
        -fomit-frame-pointer    # Omit frame pointers
        -finline-functions      # Enable inlining
    )

    add_link_options(
        -Wl,--gc-sections       # Remove unused sections
        -flto                   # Link-Time Optimization
    )
endif()

pico_set_program_name(${FW_NAME} ${FW_NAME})
pico_set_program_version(${FW_NAME} ${FW_VERSION})

target_include_directories(${FW_NAME} PRIVATE 
    ${SRC}
    ${COMMON}
    ${EXTERNAL_DIR}/libxsm3
)
target_compile_definitions(${FW_NAME} PRIVATE XSM3_NO_DEBUGGING=1 XSM3_EMBEDDED=1)

if(EN_RGB)
    pico_generate_pio_header(${FW_NAME} ${SRC}/Board/Pico_WS2812/WS2812.pio)
endif()
pico_generate_pio_header(${FW_NAME} ${SRC}/USBDevice/DeviceDriver/PS1PS2/psxSPI.pio)
pico_generate_pio_header(${FW_NAME} ${SRC}/USBDevice/DeviceDriver/GameCube/joybus.pio)
pico_generate_pio_header(${FW_NAME} ${SRC}/USBDevice/DeviceDriver/N64/n64send.pio)
pico_generate_pio_header(${FW_NAME} ${SRC}/USBHost/GPIOHost/psx_host/clock.pio)
pico_generate_pio_header(${FW_NAME} ${SRC}/USBHost/GPIOHost/joybus_host/controller.pio)
pico_generate_pio_header(${FW_NAME} ${SRC}/USBHost/GPIOHost/dreamcast_host/maple_in.pio)
pico_generate_pio_header(${FW_NAME} ${SRC}/USBHost/GPIOHost/dreamcast_host/maple_out.pio)

target_include_directories(${FW_NAME} PRIVATE ${SRC}/USBHost/GPIOHost/dreamcast_host)

include_directories(${INC_DIRS_BOARD})

if(EN_USB_HOST)
    add_subdirectory(${PICO_PIO_USB_PATH} ${CMAKE_BINARY_DIR}/Pico-PIO-USB)
endif()

if(EN_BLUETOOTH)
    add_subdirectory(${BLUEPAD32_ROOT}/src/components/bluepad32 libbluepad32)
    # Propagate to bluepad32 sources (e.g. DS4 parser): CYW43-specific link + setup path
    target_compile_definitions(bluepad32 PRIVATE OGXM_BLUEPAD32_PICO_W=1)
    target_include_directories(bluepad32 PRIVATE ${SRC} ${COMMON})
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_definitions(bluepad32 PRIVATE CONFIG_OGXM_DEBUG=1)
    endif()
endif()

add_subdirectory(${LIBFIXMATH_PATH} libfixmath)

target_link_libraries(${FW_NAME} PRIVATE ${LIBS_BOARD})

target_compile_definitions(libfixmath PRIVATE
    FIXMATH_FAST_SIN
    FIXMATH_NO_64BIT
    FIXMATH_NO_CACHE
    FIXMATH_NO_HARD_DIVISION
    FIXMATH_NO_OVERFLOW
    # FIXMATH_NO_ROUNDING
    # FIXMATH_OPTIMIZE_8BIT
)

if(OGXM_RETAIL STREQUAL "TRUE")
    set(EXE_FILENAME "${FW_NAME}-${FW_VERSION}-${OGXM_BOARD}${BUILD_STR}-Retail")
else()
    set(EXE_FILENAME "${FW_NAME}-${FW_VERSION}-${OGXM_BOARD}${BUILD_STR}")
endif()

# Output filename always reflects the mode: fixed driver name (XINPUT, PS3, ...) or "Multi" for combo mode
if(OGXM_FIXED_DRIVER)
    set(EXE_FILENAME "${EXE_FILENAME}-${OGXM_FIXED_DRIVER}")
    if(OGXM_FIXED_DRIVER_ALLOW_COMBOS)
        set(EXE_FILENAME "${EXE_FILENAME}-Combos")
    endif()
else()
    set(EXE_FILENAME "${EXE_FILENAME}-Multi")
endif()

set_target_properties(${FW_NAME} PROPERTIES OUTPUT_NAME ${EXE_FILENAME})

pico_add_extra_outputs(${FW_NAME})