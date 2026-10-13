#ifndef _OGXM_BLE_SERVER_H_
#define _OGXM_BLE_SERVER_H_

/* The web app over Bluetooth LE (GATT: att_delayed_response.gatt next to this file), shared by
 * the Pico W and the hybrid's ESP32; settings through Bluepad32/BleStore.h. */
namespace BLEServer {
    void init_server();
}

#endif // _OGXM_BLE_SERVER_H_
