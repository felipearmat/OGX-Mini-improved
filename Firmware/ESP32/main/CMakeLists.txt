include(${CMAKE_CURRENT_SOURCE_DIR}/../../FWDefines.cmake)

idf_component_register( 
    SRCS    
        "main.c" 
        "main.cpp"
        "Board/board_api.cpp"
        "BTManager/BTManager.cpp"
        "BTManager/BTManager_BP32.cpp"
        "BLEServer/BLEServer.cpp"
        "I2CDriver/I2CDriver.cpp"
        "UserSettings/UserSettings.cpp"
        # Shared with the RP2040 firmware (Firmware/common/README.md)
        "../../common/UserSettings/UserProfile.cpp"
        "../../common/UserSettings/TriggerSettings.cpp"
        "../../common/UserSettings/JoystickSettings.cpp"
    INCLUDE_DIRS 
        "."
        "../../common"
    REQUIRES 
        bluepad32 
        btstack 
        driver 
        nvs_flash 
        libfixmath 
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
        FIRMWARE_NAME=\"${FW_NAME}\"
        FIRMWARE_VERSION=\"${FW_VERSION}\"
)