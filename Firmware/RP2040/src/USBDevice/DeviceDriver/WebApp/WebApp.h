#ifndef _WEBAAPP_DEVICE_H_
#define _WEBAAPP_DEVICE_H_

#include <array>

#include "USBDevice/DeviceDriver/DeviceDriver.h"
#include "UserSettings/UserSettings.h"
#include "UserSettings/UserProfile.h"
#include "UserSettings/WebAppProtocol.h"

class WebAppDevice : public DeviceDriver 
{
public:
    void initialize() override;
    void process(const uint8_t idx, Gamepad& gamepad) override;
    uint16_t get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) override;
    void set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) override;
    bool vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) override;
    const uint16_t* get_descriptor_string_cb(uint8_t index, uint16_t langid) override;
    const uint8_t* get_descriptor_device_cb() override;
    const uint8_t* get_hid_descriptor_report_cb(uint8_t itf)  override;
    const uint8_t* get_descriptor_configuration_cb(uint8_t index) override;
    const uint8_t* get_descriptor_device_qualifier_cb() override;

    void line_state_cb(uint8_t itf, bool dtr, bool rts) override;

    /* Custom: the web app has the serial port open (D-Input/Debug mode). */
    static bool connected() { return tud_cdc_connected(); }

private:
    using PacketID = webapp_protocol::PacketID;  // the wire format: UserSettings/WebAppProtocol.h
    
    #pragma pack(push, 1)
    using SystemCommand = webapp_protocol::SystemCommand;

    struct PacketHeader
    {
        uint8_t packet_len{64};
        PacketID packet_id{PacketID::NONE};
        DeviceDriverType device_driver{DeviceDriverType::DINPUT_DEBUG};  // Custom: the mode the web app runs in (D-Input/Debug)
        uint8_t max_gamepads{MAX_GAMEPADS};
        uint8_t player_idx{0};
        uint8_t profile_id{0};
        uint8_t chunks_total{0};
        uint8_t chunk_idx{0};
        uint8_t chunk_len{0};
    };
    static_assert(sizeof(PacketHeader) == 9, "WebApp report size mismatch");

    struct Packet
    {
        PacketHeader header;
        std::array<uint8_t, 64 - sizeof(PacketHeader)> data{0};
    };
    static_assert(sizeof(Packet) == 64, "WebApp report size mismatch");
    #pragma pack(pop)

    UserSettings& user_settings_{UserSettings::get_instance()};
    /* Custom: rumble test end time (ms since boot), 0 = none running. */
    uint32_t rumble_test_until_ms_{0};
    /* Custom: end of the colour preview (ms since boot), 0 = none. */
    static constexpr uint32_t kPreviewMs = 15000;
    uint32_t preview_until_ms_{0};

    /* Custom: live input to the web app is capped (kLiveIntervalMs) and the touchpad sent only
     * when it changes: a 250 Hz controller plus its touchpad made ~32 KB/s, more than the page
     * reads, and the browser dropped the serial port. */
    static constexpr uint32_t kLiveIntervalMs = 10;
    static constexpr uint32_t kTouchRefreshMs = 500;
    Gamepad::PadIn live_in_[MAX_GAMEPADS]{};
    bool live_pending_[MAX_GAMEPADS]{};
    uint32_t live_sent_ms_[MAX_GAMEPADS]{};
    uint8_t touch_sent_[MAX_GAMEPADS][9]{};
    uint32_t touch_sent_ms_[MAX_GAMEPADS]{};
    void send_live_input(uint8_t idx, Gamepad& gamepad);
    UserProfile profile_;

    bool read_profile(UserProfile& profile);
    bool read_serial(void* buffer, size_t len, bool block);
    bool read_packet(Packet& packet, bool block);
    bool write_serial(const void* buffer, size_t len);
    bool write_packet(const Packet& packet);
    bool write_profile(uint8_t index, const UserProfile& profile, PacketID packet_id);
    bool write_gamepad(uint8_t index, const Gamepad::PadIn& pad_in);
    void write_error();
    bool write_dongle_settings();
    bool write_kbm_settings();
    bool write_led_colors();
    void end_preview();
    bool write_diagnostics();
    bool write_log(uint32_t since);
    bool write_flash_log(uint32_t from_seq);
    bool write_chunked(PacketID id, const char* data, size_t len);  // up to 255 chunks
    bool write_touch(uint8_t index, const Gamepad::PadIn& pad_in);
};

#endif // _WEBAAPP_DEVICE_H_
