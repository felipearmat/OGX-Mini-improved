// Virtual adapter: no radio; the calls UserSettings makes on Bluetooth boards.
#pragma once
namespace bluepad32 {
inline void disconnect_pads_before_reboot() {}
inline void request_stop_search() {}
}
