#ifndef _OGXM_BLE_SERVER_H_
#define _OGXM_BLE_SERVER_H_

/* The web app over Bluetooth LE (GATT: att_delayed_response.gatt next to this file), shared by
 * the Pico W and the hybrid's ESP32; settings through Bluepad32/BleStore.h. */
namespace BLEServer {
    void init_server();

    /* Advertising for the web app, set by the search policy (Bluepad32/ScanControl): fast while
     * the adapter searches at full rate, slow (a tenth: 300 ms) during the reduced search, off
     * while every slot is in use or a BLE controller is connected (it shares the one radio). */
    enum class Advertising { Fast, Slow, Off };
    void set_advertising(Advertising mode);
}

#endif // _OGXM_BLE_SERVER_H_
