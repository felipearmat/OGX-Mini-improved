#ifndef _BT_MANAGER_H_
#define _BT_MANAGER_H_

#include <cstdint>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "I2CDriver/I2CDriver.h"
#include "Gamepad/RumbleTiming.h"
#include "Gamepad/Gamepad.h"
#include "Gamepad/Lightbar.h"
#include "Bluepad32/BtPad.h"
#include "Bluepad32/ComboHold.h"
#include "Bluepad32/IdleOff.h"
#include "UserSettings/ComboCheck.h"

/* The ESP32 side of the RP2040 + ESP32 hybrid: Bluepad32 pads in, I2C packets out. How a pad
 * behaves comes from Firmware/common/Bluepad32 (shared with the Pico W path); this class keeps the
 * Bluetooth slots, the outputs they feed, and the I2C traffic. */
class BTManager
{
public:
    static BTManager& get_instance()
    {
        static BTManager instance;
        return instance;
    }

    void run_task();
    bool any_connected();
    bool is_connected(uint8_t index);
    I2CDriver::PacketIn get_packet_in(uint8_t index);

    /* A profile or the output mode was saved (web app over BLE): apply it now. */
    void on_settings_saved();

    /* For the BLE web app (BLEServer/BleStore.cpp): the RP2040's mouse + keyboard mapping (fetched
     * every few seconds), and settings saved over BLE sent to it. */
    size_t kbm_settings(uint8_t* out, size_t len);
    void send_settings_blob(uint8_t kind, const uint8_t* data, size_t len);

private:
    BTManager() = default;
    ~BTManager() = default;
    BTManager(const BTManager&) = delete;
    BTManager& operator=(const BTManager&) = delete;

    /* Rumble read from the RP2040 every feedback period and played for longer than one period
     * (Gamepad/RumbleTiming.h, shared with the Pico W path): played for exactly one period, a
     * long rumble stopped and restarted every cycle and felt weak. */
    static constexpr uint32_t FEEDBACK_TIME_MS = switch_rumble::kFeedbackPeriodMs;
    static constexpr uint32_t RUMBLE_DURATION_MS = switch_rumble::kRumbleDurationMs;
    static constexpr uint32_t LED_CHECK_TIME_MS = 500;
    static constexpr uint16_t STOP_SEARCH_RUMBLE_MS = 400;

    /* One Bluetooth slot (Bluepad32 device index). */
    struct Slot
    {
        bool connected{false};
        bool was_ready{false};          // reached "ready": its disconnect restarts the ESP32
        int output{-1};                 // output it feeds
        uint32_t host_rumble_ok_ms{0};  // DualShock 4: no host rumble before this (btstack time)
        ComboHold combo;                // disconnect combo (Bluepad32/BtPad.h)
        bt_pad::KeepaliveState keepalive;
        Lightbar lightbar;              // last colour sent
        uint8_t battery_sent{0};        // last battery level sent to the diagnostics
        btstack_timer_source_t mode_lightbar_timer;
    };

    /* One output: a USB pad on the RP2040. A merged Joy-Con pair feeds one. */
    struct Output
    {
        std::atomic<bool> connected{false};
        GamepadMapper mapper;
        I2CDriver::PacketIn packet_in;  // last sent to the RP2040
        I2CDriver::PacketInExt ext;     // last sent to the RP2040
        std::atomic<I2CDriver::PacketOut> packet_out; //Can be updated from i2c thread
        Lightbar host_lightbar;         // asked by the host (PS4 mode)
        IdleOff idle;
    };

    struct FBContext
    {
        uint8_t index;
        btstack_context_callback_registration_t cb_reg;
    };

    std::array<Slot, CONFIG_BLUEPAD32_MAX_DEVICES> slots_;
    std::array<Output, MAX_GAMEPADS> outputs_;
    I2CDriver i2c_driver_;

    /* From the RP2040 (GET_EXT): host lightbar, dongle settings, whether the mode uses motion.
     * Written on the I2C task, read on the BTstack run loop. */
    I2CDriver::PacketOutExt ext_rx_;
    std::mutex ext_mutex_;
    FBContext ext_context_;
    bool motion_output_{false};
    int high_tx_power_{-1};  // applied transmit power option (-1: not yet)
    void apply_tx_power(bool high);
    void send_companion_summary();

    /* Settings kept by the RP2040: the copy taken (checksum + active profile), a fetch running,
     * and a mode change from a combo waiting for the RP2040 to take it. */
    uint16_t profiles_crc_{0};
    uint8_t profiles_active_{0};
    uint16_t fetch_crc_{0};         // the copy being fetched
    uint8_t fetch_active_{0};
    bool profiles_fetching_{false};
    uint32_t profiles_fetch_ms_{0};
    I2CDriver::Profiles profiles_rx_;
    btstack_context_callback_registration_t profiles_cb_reg_;
    DeviceDriverType pending_driver_{DeviceDriverType::NONE};
    uint8_t pending_driver_ticks_{0};
    uint8_t kbm_rx_[I2CDriver::PacketBlob::kMaxLen]{};
    size_t kbm_rx_len_{0};
    std::mutex kbm_mutex_;

    btstack_timer_source_t fb_timer_;
    bool fb_timer_running_ = false;
    btstack_timer_source_t led_timer_;
    btstack_timer_source_t disconnect_combo_timer_;
    int disconnect_combo_idx_ = -1;
    mode_combos::ComboCheck combo_check_;

    static uint8_t slave_addr(uint8_t output);
    void send_driver_type(DeviceDriverType driver_type);
    void send_neutral(uint8_t output);
    void apply_profiles();
    void apply_scan(bool pad_leaving, bool force);
    void apply_ext(const I2CDriver::PacketOutExt& ext);
    void adopt_driver(DeviceDriverType driver, bool mode_change);
    static void profiles_cb(void* context);
    int resolve_output(uni_hid_device_t* device, int slot);
    bool lone_joycon();
    void schedule_mode_lightbar(uni_hid_device_t* device, int slot);
    void schedule_disconnect_combo(uint8_t slot);
    void stop_search_feedback();
    /* Diagnostics for the web app's report, kept by the RP2040 (I2C/Esp32Bridge.h SET_DIAG). */
    void diag_send(uint8_t kind, uint8_t slot, const void* data, size_t len);
    void diag_event(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    static uni_hid_device_t* get_connected_bp32_device(uint8_t slot);
    static void check_led_cb(btstack_timer_source *ts);
    static void begin_mode_indicator(DeviceDriverType driver);
    static void mode_lightbar_cb(btstack_timer_source *ts);
    static void output_feedback_cb(void* context);
    static void ext_feedback_cb(void* context);
    static void feedback_timer_cb(btstack_timer_source *ts);
    static void driver_update_timer_cb(btstack_timer_source *ts);
    static void disconnect_combo_timer_cb(btstack_timer_source *ts);
    static void restart_after_ms(uint32_t delay_ms, bool last_pad = false);
    static bool cancel_last_pad_restart();
    static void scan_event(uint32_t now_ms, const char* text);

    //Bluepad32 driver

    void init(int argc, const char** arg_V);
    void init_complete_cb(void);
    uni_error_t device_discovered_cb(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi);
    void device_connected_cb(uni_hid_device_t* device);
    void device_disconnected_cb(uni_hid_device_t* device);
    uni_error_t device_ready_cb(uni_hid_device_t* device);
    void controller_data_cb(uni_hid_device_t* bp_device, uni_controller_t* controller);
    const uni_property_t* get_property_cb(uni_property_idx_t idx);
    void oob_event_cb(uni_platform_oob_event_t event, void* data);

    static uni_platform* get_bp32_driver();

}; // class BTManager

#endif // _BT_MANAGER_H_
