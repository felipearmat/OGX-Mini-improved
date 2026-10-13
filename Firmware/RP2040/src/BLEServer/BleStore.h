#pragma once

#include "Gamepad/Gamepad.h"

/* The Pico W's storage for the shared BLE web app (Bluepad32/BleStore.h): its own flash. */
namespace ble_store {
    void init(Gamepad* pad);  // before BLEServer::init_server(): pad 0 for the live input
}
