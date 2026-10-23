#include <algorithm>
#include <cstdio>
#include "class/cdc/cdc_device.h"
#include "bsp/board_api.h"

#include <pico/bootrom.h>
#include <pico/stdlib.h>

#include "Board/board_api.h"
#include "Board/BootMode.h"
#include "UserSettings/Capabilities.h"
#include "Board/ogxm_log.h"
#include "Diagnostics/Diagnostics.h"
#include "Board/LogRing.h"
#include "Board/FlashLog.h"
#include "Descriptors/CDCDev.h"
#include "Board/ModeIndicator.h"
#include "UserSettings/BoardCapabilities.h"
#include "Gamepad/I2CWirePad.h"
#include "Gamepad/MotionImu.h"
#include "USBDevice/DeviceDriver/WebApp/WebApp.h"

void WebAppDevice::initialize() 
{
    class_driver_ = 
    {
        .name = TUD_DRV_NAME("WEBAPP"),
        .init = cdcd_init,
        .deinit = cdcd_deinit,
        .reset = cdcd_reset,
        .open = cdcd_open,
        .control_xfer_cb = cdcd_control_xfer_cb,
        .xfer_cb = cdcd_xfer_cb,
        .sof = NULL
    };
}

bool WebAppDevice::read_serial(void* buffer, size_t len, bool block)
{
    if (!block && !tud_cdc_available()) 
    {
        return false;
    }

    uint8_t* buf_ptr = reinterpret_cast<uint8_t*>(buffer);
    size_t total_read = 0;

    while (total_read < len)
    {
        if (!tud_cdc_connected())
        {
            return false;
        }
        tud_task();

        if (tud_cdc_available())
        {
            size_t read = tud_cdc_read(buf_ptr + total_read, len - total_read);
            total_read += read;
        }
    }
    OGXM_LOG("Read %d bytes\n", total_read);
    OGXM_LOG_HEX(buf_ptr, total_read);

    return total_read == len;
}

bool WebAppDevice::write_serial(const void* buffer, size_t len)
{
    const uint8_t* buf_ptr = reinterpret_cast<const uint8_t*>(buffer);
    size_t total_written = 0;
    /* Custom fix: a host that opens the port and stops reading (a frozen page, a tool holding the
     * port) filled the transmit buffer, and this loop waited for it forever: the main loop hung
     * until the hang watchdog restarted the adapter ("main loop: USB"). Give up after a while. */
    constexpr uint32_t kWriteTimeoutMs = 100;
    const uint32_t start_ms = board_api::ms_since_boot();

    while (total_written < len)
    {
        if (!tud_cdc_connected() || board_api::ms_since_boot() - start_ms >= kWriteTimeoutMs)
        {
            return false;
        }
        tud_task();

        if (tud_cdc_write_available())
        {
            size_t written = tud_cdc_write(buf_ptr + total_written, len - total_written);
            total_written += written;
            tud_cdc_write_flush();
        }
    }
    OGXM_LOG("Wrote %d bytes\n", total_written);
    OGXM_LOG_HEX(buf_ptr, total_written);

    return total_written == len;
}

bool WebAppDevice::write_packet(const Packet& packet)
{
    if (!write_serial(&packet, sizeof(Packet)))
    {
        return false;
    }
    return true;
}

bool WebAppDevice::read_packet(Packet& packet, bool block)
{
    if (!read_serial(&packet, sizeof(Packet), block))
    {
        return false;
    }
    return true;
}

//Blocking read
bool WebAppDevice::read_profile(UserProfile& profile)
{
    Packet packet_out;
    uint8_t* profile_data = reinterpret_cast<uint8_t*>(&profile);
    uint8_t current_chunk = 0;
    uint8_t expected_chunks = 0;
    size_t profile_offset = 0;

    while (current_chunk < expected_chunks || expected_chunks == 0)
    {
        if (!read_packet(packet_out, true))
        {
            OGXM_LOG("Failed to read profile packet\n");
            return false;
        }
        if (packet_out.header.packet_id != PacketID::SET_PROFILE)
        {
            OGXM_LOG("Invalid packet ID: %i\n", packet_out.header.packet_id);
            return false;
        }
        if (expected_chunks == 0 && packet_out.header.chunks_total > 0)
        {
            expected_chunks = packet_out.header.chunks_total;
            OGXM_LOG("Expected chunks: %d\n", expected_chunks);
        }
        else if (expected_chunks == 0)
        {
            OGXM_LOG("Invalid chunk count\n");
            return false;
        }
        if (packet_out.header.chunk_len > sizeof(UserProfile) - profile_offset)
        {
            OGXM_LOG("Invalid chunk length\n");
            return false;
        }

        OGXM_LOG("Reading chunk %d\n", current_chunk);
        OGXM_LOG("Chunks IDX: %d\n", packet_out.header.chunk_idx);

        std::memcpy(profile_data + profile_offset, packet_out.data.data(), packet_out.header.chunk_len);
        current_chunk++;
        profile_offset += packet_out.header.chunk_len;
    }
    OGXM_LOG("Profile read successfully\n");
    return true;
}

bool WebAppDevice::write_profile(uint8_t index, const UserProfile& profile, PacketID packet_id)
{
    Packet packet_in;
    const uint8_t* profile_data = reinterpret_cast<const uint8_t*>(&profile);
    uint8_t total_chunks = static_cast<uint8_t>((sizeof(UserProfile) + packet_in.data.size() - 1) / packet_in.data.size());
    uint8_t current_chunk = 0;

    OGXM_LOG("Writing profile\n");

    packet_in.header.packet_id = packet_id;
    packet_in.header.max_gamepads = MAX_GAMEPADS;
    packet_in.header.player_idx = index;
    packet_in.header.profile_id = profile.id;
    packet_in.header.chunks_total = total_chunks;

    while (current_chunk < total_chunks)
    {
        size_t offset = current_chunk * packet_in.data.size();
        size_t remaining_bytes = sizeof(UserProfile) - offset;
        uint8_t current_chunk_len = static_cast<uint8_t>(std::min(packet_in.data.size(), remaining_bytes));

        packet_in.header.chunk_idx = current_chunk;
        packet_in.header.chunk_len = current_chunk_len;

        std::memcpy(packet_in.data.data(), profile_data + offset, packet_in.header.chunk_len);

        if (!write_packet(packet_in))
        {
            OGXM_LOG("Failed to write profile packet\n");
            return false;
        }
        current_chunk++;
    }

    OGXM_LOG("Profile ID %i written successfully\n", profile.id);
    return true;
}

bool WebAppDevice::write_gamepad(uint8_t index, const Gamepad::PadIn& pad_in)
{
    Packet packet_in;
    /* Custom fix: the web app only accepts the legacy 23-byte pad layout; PadIn grew IMU and
     * touchpad fields, so it rejected every input packet ("Invalid data length"). */
    const I2CWirePadIn wire = i2c_wire_pad_from(pad_in);
    const uint8_t* pad_in_data = reinterpret_cast<const uint8_t*>(&wire);
    const uint8_t total_chunks = static_cast<uint8_t>((sizeof(wire) + packet_in.data.size() - 1) / packet_in.data.size());
    uint8_t current_chunk = 0;

    /* Live input goes out whole or not at all: with no room (the host not reading) it waits for
     * the next pass, which sends the newer state. */
    if (tud_cdc_write_available() < total_chunks * sizeof(Packet))
        return false;
    packet_in.header.packet_id = PacketID::SET_GP_IN;
    packet_in.header.max_gamepads = MAX_GAMEPADS;
    packet_in.header.player_idx = index;
    packet_in.header.chunks_total = total_chunks;

    while (current_chunk < total_chunks)
    {
        size_t offset = current_chunk * packet_in.data.size();
        size_t remaining_bytes = sizeof(wire) - offset;
        uint8_t current_chunk_len = static_cast<uint8_t>(std::min(packet_in.data.size(), remaining_bytes));

        packet_in.header.chunk_idx = current_chunk;
        packet_in.header.chunk_len = current_chunk_len;

        std::memcpy(packet_in.data.data(), pad_in_data + offset, packet_in.header.chunk_len);

        if (!write_packet(packet_in))
        {
            return false;
        }
        current_chunk++;
    }
    return true;
}

/* Custom: touchpad for the web app's tracking view (GP_TOUCH). */
bool WebAppDevice::write_touch(uint8_t index, const Gamepad::PadIn& pad_in)
{
    if (tud_cdc_write_available() < sizeof(Packet))
        return false;  // as write_gamepad: next pass
    Packet packet_in;
    packet_in.header.packet_id = PacketID::GP_TOUCH;
    packet_in.header.max_gamepads = MAX_GAMEPADS;
    packet_in.header.player_idx = index;
    packet_in.header.chunks_total = 1;
    packet_in.header.chunk_idx = 0;
    packet_in.header.chunk_len = sizeof(pad_in.touch_raw) + 1;
    std::memcpy(packet_in.data.data(), pad_in.touch_raw, sizeof(pad_in.touch_raw));
    packet_in.data[sizeof(pad_in.touch_raw)] = pad_in.touchpad_click;
    return write_packet(packet_in);
}

/* Custom: motion for the web app's motion view (GP_MOTION), in the common frame. */
bool WebAppDevice::write_motion(uint8_t index, const Gamepad::PadIn& pad_in)
{
    if (tud_cdc_write_available() < sizeof(Packet))
        return false;  // as write_gamepad: next pass
    int32_t accel[3], gyro[3];
    for (int i = 0; i < 3; ++i) {
        accel[i] = pad_in.accel[i];
        gyro[i] = pad_in.gyro[i];
    }
    MotionImu::remap_to_ds4_report_frame(pad_in.motion_source, accel, gyro);
    Packet packet_in;
    packet_in.header.packet_id = PacketID::GP_MOTION;
    packet_in.header.max_gamepads = MAX_GAMEPADS;
    packet_in.header.player_idx = index;
    packet_in.header.chunks_total = 1;
    packet_in.header.chunk_idx = 0;
    packet_in.header.chunk_len = 17;
    uint8_t* d = packet_in.data.data();
    auto put16 = [&](size_t at, int64_t v) {
        const int16_t c = static_cast<int16_t>(std::clamp<int64_t>(v, INT16_MIN, INT16_MAX));
        d[at] = static_cast<uint8_t>(c);
        d[at + 1] = static_cast<uint8_t>(static_cast<uint16_t>(c) >> 8);
    };
    d[0] = pad_in.motion_source;
    for (int i = 0; i < 3; ++i) {
        put16(1 + 2 * i, static_cast<int64_t>(accel[i]) * 1000 / 8192);  // DS4 units: 8192 / g
        put16(7 + 2 * i, static_cast<int64_t>(gyro[i]) * 10 / 1024);     // 1024 / (deg/s)
    }
    const uint32_t now = board_api::ms_since_boot();
    for (int i = 0; i < 4; ++i)
        d[13 + i] = static_cast<uint8_t>(now >> (8 * i));
    return write_packet(packet_in);
}

/* Custom: dongle options, one packet (version byte first). */
bool WebAppDevice::write_dongle_settings()
{
    Packet packet_in;
    const dongle_settings::Settings& settings = dongle_settings::get();
    packet_in.header.packet_id = PacketID::GET_DONGLE_SETTINGS;
    packet_in.header.max_gamepads = MAX_GAMEPADS;
    packet_in.header.chunks_total = 1;
    packet_in.header.chunk_idx = 0;
    packet_in.header.chunk_len = sizeof(settings);
    std::memcpy(packet_in.data.data(), &settings, sizeof(settings));
    return write_packet(packet_in);
}

/* Custom: mouse + keyboard mapping, one packet (version byte first). */
bool WebAppDevice::write_kbm_settings()
{
    static_assert(sizeof(kbm_settings::Settings) <= sizeof(Packet::data), "KBM settings fit one packet");
    Packet packet_in;
    const kbm_settings::Settings& settings = kbm_settings::get();
    packet_in.header.packet_id = PacketID::GET_KBM_SETTINGS;
    packet_in.header.max_gamepads = MAX_GAMEPADS;
    packet_in.header.chunks_total = 1;
    packet_in.header.chunk_idx = 0;
    packet_in.header.chunk_len = sizeof(settings);
    std::memcpy(packet_in.data.data(), &settings, sizeof(settings));
    return write_packet(packet_in);
}

bool WebAppDevice::write_led_colors()
{
    static_assert(sizeof(mode_indicator::ModeColors) <= sizeof(Packet::data), "mode colours fit one packet");
    Packet packet_in;
    const mode_indicator::ModeColors& colors = mode_indicator::custom_colors();
    packet_in.header.packet_id = PacketID::GET_LED_COLORS;
    packet_in.header.max_gamepads = MAX_GAMEPADS;
    packet_in.header.chunks_total = 1;
    packet_in.header.chunk_idx = 0;
    packet_in.header.chunk_len = sizeof(colors);
    std::memcpy(packet_in.data.data(), &colors, sizeof(colors));
    return write_packet(packet_in);
}

void WebAppDevice::end_preview()
{
    preview_until_ms_ = 0;
    mode_indicator::set_override(false, 0, 0, 0);
}

/* Custom: diagnostics report as JSON text, split over packets (up to 255 chunks). */
bool WebAppDevice::write_diagnostics()
{
    /* Up to 255 chunks; the RP2350 keeps more events (Diagnostics/Diagnostics.h kEvents). */
#if defined(PICO_RP2350)
    static char report[255 * 55];
#else
    static char report[12 * 1024];
#endif
    size_t len = diag::report_json(report, sizeof(report), board_api::ms_since_boot());
    /* Custom: the kept log in flash (Board/FlashLog.h), before the report's closing brace. */
    if (len > 0 && report[len - 1] == '}')
    {
        const flash_log::Stats st = flash_log::stats();
        const int n = std::snprintf(report + len - 1, sizeof(report) - len + 1,
            ",\"flash_log\":{\"enabled\":%s,\"available\":%s,\"pages\":%lu,\"capacity\":%lu,"
            "\"oldest_seq\":%lu,\"newest_seq\":%lu,\"ready\":%lu,\"dropped_bytes\":%lu,\"boot\":%u}}",
            st.enabled ? "true" : "false", st.available ? "true" : "false",
            static_cast<unsigned long>(st.pages), static_cast<unsigned long>(st.capacity),
            static_cast<unsigned long>(st.oldest_seq), static_cast<unsigned long>(st.newest_seq),
            static_cast<unsigned long>(st.ready), static_cast<unsigned long>(st.dropped), st.boot);
        if (n > 0 && static_cast<size_t>(n) < sizeof(report) - len + 1)
            len += static_cast<size_t>(n) - 1;
        else
            report[len - 1] = '}';  // no room: the report as it was
    }
    return write_chunked(PacketID::GET_DIAGNOSTICS, report, len);
}

/* Custom: the kept log from `since` on (WebAppProtocol.h GET_LOG). */
bool WebAppDevice::write_log(uint32_t since)
{
    static uint8_t answer[8 + webapp_protocol::kLogBytes];
    uint32_t start = 0, total = 0;
    const size_t n = log_ring_read_since(since, &start, &total, answer + 8, webapp_protocol::kLogBytes);
    std::memcpy(answer, &start, 4);
    std::memcpy(answer + 4, &total, 4);
    return write_chunked(PacketID::GET_LOG, reinterpret_cast<const char*>(answer), 8 + n);
}

/* Custom: a page of the kept log in flash (WebAppProtocol.h GET_FLASH_LOG). */
bool WebAppDevice::write_flash_log(uint32_t from_seq)
{
    static uint8_t answer[webapp_protocol::kFlashLogHeader + flash_log::kPageText];
    flash_log::Page page;
    const bool found = flash_log::read(from_seq, page);
    const uint32_t seq = found ? page.seq : 0;
    const uint32_t gen = flash_log::stats().ring_gen;
    std::memset(answer, 0, webapp_protocol::kFlashLogHeader);
    std::memcpy(answer, &seq, 4);
    size_t len = webapp_protocol::kFlashLogHeader;
    if (found) {
        std::memcpy(answer + 4, &page.boot, 2);
        std::memcpy(answer + 6, &page.ring_gen, 4);
        std::memcpy(answer + 10, &page.ring_pos, 4);
        std::memcpy(answer + 14, &page.uptime_s, 4);
        std::memcpy(answer + webapp_protocol::kFlashLogHeader, page.text, page.len);
        len += page.len;
    }
    std::memcpy(answer + 18, &gen, 4);
    return write_chunked(PacketID::GET_FLASH_LOG, reinterpret_cast<const char*>(answer), len);
}

bool WebAppDevice::write_chunked(PacketID id, const char* data, size_t len)
{
    Packet packet_in;
    const size_t chunk = packet_in.data.size();
    const size_t total = (len + chunk - 1) / chunk;
    if (total == 0 || total > 255)
        return false;
    packet_in.header.packet_id = id;
    packet_in.header.max_gamepads = MAX_GAMEPADS;
    packet_in.header.chunks_total = static_cast<uint8_t>(total);
    for (size_t i = 0; i < total; ++i)
    {
        const size_t offset = i * chunk;
        const size_t n = std::min(chunk, len - offset);
        packet_in.header.chunk_idx = static_cast<uint8_t>(i);
        packet_in.header.chunk_len = static_cast<uint8_t>(n);
        std::memcpy(packet_in.data.data(), data + offset, n);
        if (!write_packet(packet_in))
            return false;
    }
    return true;
}

void WebAppDevice::write_error()
{
    Packet packet_in;
    packet_in.header.chunks_total = 1; 
    packet_in.header.packet_id = PacketID::RESP_ERROR;
    write_packet(packet_in);
}

void WebAppDevice::process(const uint8_t idx, Gamepad& gamepad) 
{
    diag::tick(board_api::ms_since_boot());  // Custom: rates for boards without Bluetooth too
    /* Custom: end a rumble test once its time is up (the pad keeps the last request otherwise). */
    if (rumble_test_until_ms_ != 0 &&
        static_cast<int32_t>(board_api::ms_since_boot() - rumble_test_until_ms_) >= 0)
    {
        rumble_test_until_ms_ = 0;
        gamepad.set_pad_out(Gamepad::PadOut());
    }

    /* Custom: a colour preview ends on its own, or with the web app's connection. */
    if (preview_until_ms_ != 0 &&
        (!tud_cdc_connected() || static_cast<int32_t>(board_api::ms_since_boot() - preview_until_ms_) >= 0))
    {
        end_preview();
    }

    if (!tud_cdc_connected())
    {
        return;
    }

    tud_cdc_write_flush();
    bool success = false;   

    if (tud_cdc_available())
    {
        OGXM_LOG("Reading packet\n");

        Packet packet_out;
        if (!read_packet(packet_out, true))
        {
            return;
        }

        OGXM_LOG("Received packet with ID: %d\n", packet_out.header.packet_id);
        
        switch (packet_out.header.packet_id)
        {
            case PacketID::GET_PROFILE_BY_ID:
                OGXM_LOG("Getting profile by ID: %i\n", packet_out.header.profile_id);

                profile_ = user_settings_.get_profile_by_id(packet_out.header.profile_id);
                if (!write_profile(0, profile_, PacketID::GET_PROFILE_BY_ID))
                {
                    write_error();
                    return;
                }
                break;

            case PacketID::GET_PROFILE_BY_IDX:
                OGXM_LOG("Getting profile by index: %i\n", packet_out.header.player_idx);

                profile_ = user_settings_.get_profile_by_index(packet_out.header.player_idx);
                if (!write_profile(packet_out.header.player_idx, profile_, PacketID::GET_PROFILE_BY_IDX))
                {
                    write_error();
                    return;
                }
                break;

            case PacketID::SET_PROFILE_START:
                if (!read_profile(profile_))
                {
                    write_error();
                    return;
                }
                /* Custom: a mode other than the current one (D-Input/Debug, where the web app runs)
                 * is stored with the profile and the adapter restarts in it. */
                if (normalized(packet_out.header.device_driver) != user_settings_.get_current_driver() &&
                    user_settings_.is_valid_driver(packet_out.header.device_driver))
                {
                    success = user_settings_.store_profile_and_driver_type(packet_out.header.device_driver, packet_out.header.player_idx, profile_);
                }
                else
                {
                    success = user_settings_.store_profile(packet_out.header.player_idx, profile_);
                }
                if (!success)
                {
                    write_error();
                    return;
                }
                break;

            case PacketID::GET_DONGLE_SETTINGS:
                if (!write_dongle_settings())
                {
                    write_error();
                    return;
                }
                break;

            case PacketID::SET_DONGLE_SETTINGS:
            {
                dongle_settings::Settings settings{};
                if (!dongle_settings::decode(packet_out.data.data(), packet_out.header.chunk_len, settings))
                {
                    write_error();
                    return;
                }
                user_settings_.store_dongle_settings(settings);  // reboots
                break;
            }

            case PacketID::GET_KBM_SETTINGS:
                if (!write_kbm_settings())
                {
                    write_error();
                    return;
                }
                break;

            case PacketID::SET_KBM_SETTINGS:
            {
                kbm_settings::Settings settings{};
                if (!kbm_settings::decode(packet_out.data.data(), packet_out.header.chunk_len, settings))
                {
                    write_error();
                    return;
                }
                user_settings_.store_kbm_settings(settings);
                if (!write_kbm_settings())
                {
                    write_error();
                    return;
                }
                break;
            }

            case PacketID::GET_LED_COLORS:
                if (!write_led_colors())
                {
                    write_error();
                    return;
                }
                break;

            case PacketID::SET_LED_COLORS:
            {
                mode_indicator::ModeColors colors{};
                if (!mode_indicator::decode_colors(packet_out.data.data(), packet_out.header.chunk_len, colors))
                {
                    write_error();
                    return;
                }
                user_settings_.store_led_colors(colors);
                if (!write_led_colors())
                {
                    write_error();
                    return;
                }
                break;
            }

            case PacketID::LED_PREVIEW:
            {
                if (packet_out.header.chunk_len < 4)
                {
                    write_error();
                    return;
                }
                if (packet_out.data[0])
                {
                    mode_indicator::set_override(true, packet_out.data[1], packet_out.data[2], packet_out.data[3]);
                    const uint32_t until = board_api::ms_since_boot() + kPreviewMs;
                    preview_until_ms_ = until != 0 ? until : 1;
                }
                else
                {
                    end_preview();
                }
                Packet ack;
                ack.header.packet_id = PacketID::LED_PREVIEW;
                ack.header.chunks_total = 1;
                ack.header.chunk_len = 0;
                write_packet(ack);
                break;
            }

            case PacketID::GET_DIAGNOSTICS:
                if (!write_diagnostics())
                {
                    write_error();
                    return;
                }
                break;

            case PacketID::GET_FLASH_LOG:
            {
                uint32_t from = 0;
                if (packet_out.header.chunk_len >= 4)
                    std::memcpy(&from, packet_out.data.data(), 4);
                if (!write_flash_log(from))
                {
                    write_error();
                    return;
                }
                break;
            }

            case PacketID::GET_LOG:
            {
                uint32_t since = 0;
                if (packet_out.header.chunk_len >= 4)
                    std::memcpy(&since, packet_out.data.data(), 4);
                if (!write_log(since))
                {
                    write_error();
                    return;
                }
                break;
            }

            case PacketID::SET_GP_OUT:
            {
                /* Custom: rumble test, played through the same path as host rumble. */
                if (packet_out.header.chunk_len < 4)
                {
                    write_error();
                    return;
                }
                static constexpr uint16_t MAX_TEST_MS = 10000;
                uint16_t duration = static_cast<uint16_t>(packet_out.data[2] | (packet_out.data[3] << 8));
                if (duration > MAX_TEST_MS)
                    duration = MAX_TEST_MS;
                Gamepad::PadOut gp_out;
                gp_out.rumble_l = packet_out.data[0];
                gp_out.rumble_r = packet_out.data[1];
                gamepad.set_pad_out(gp_out);
                const uint32_t until = board_api::ms_since_boot() + duration;
                rumble_test_until_ms_ = until != 0 ? until : 1;
                Packet ack;
                ack.header.packet_id = PacketID::SET_GP_OUT;
                ack.header.chunks_total = 1;
                ack.header.chunk_len = 0;
                write_packet(ack);
                break;
            }

            case PacketID::GET_CAPABILITIES:
            {
                const uint32_t caps = capabilities::board() | capabilities::MODE_COLORS | capabilities::MOTION_STREAM;
                Packet reply;
                reply.header.packet_id = PacketID::GET_CAPABILITIES;
                reply.header.chunks_total = 1;
                reply.header.chunk_len = 4;
                for (int i = 0; i < 4; ++i)
                    reply.data[i] = static_cast<uint8_t>(caps >> (8 * i));
                write_packet(reply);
                break;
            }

            case PacketID::GET_VERSION:
            {
#if !defined(OGXM_BOARD_NAME)
#define OGXM_BOARD_NAME "unknown"
#endif
                char companion[40]{};
                diag::companion_version_copy(companion, sizeof(companion));
                char text[160];
                const int len = std::snprintf(text, sizeof(text), "%s%c%s%c%s", OGXM_VERSION, '\0',
                                              OGXM_BOARD_NAME, '\0', companion);
                write_chunked(PacketID::GET_VERSION, text,
                              static_cast<size_t>(len < 0 ? 0 : std::min<int>(len, sizeof(text) - 1)));
                break;
            }

            case PacketID::SYSTEM:
            {
                const uint8_t command = packet_out.header.chunk_len >= 1 ? packet_out.data[0] : 0;
#if defined(CONFIG_EN_ESP32) && !defined(CONFIG_OGXM_BOARD_ESP32_BLUERETRO_I2C)
                const bool esp32 = true;
#else
                const bool esp32 = false;
#endif
                if (command < webapp_protocol::SYSTEM_BOOTSEL || command > webapp_protocol::SYSTEM_ESP32_MONITOR ||
                    (!esp32 && command >= webapp_protocol::SYSTEM_ESP32_FLASH))
                {
                    write_error();
                    return;
                }
                Packet ack;
                ack.header.packet_id = PacketID::SYSTEM;
                ack.header.chunks_total = 1;
                ack.header.chunk_len = 1;
                ack.data[0] = command;
                write_packet(ack);
                tud_cdc_write_flush();
                for (int i = 0; i < 20; ++i)  // let the answer reach the host
                {
                    tud_task();
                    sleep_ms(5);
                }
                OGXM_LOG("System command %u\n", command);
                board_api::usb::disconnect_all();
                if (command == webapp_protocol::SYSTEM_BOOTSEL)
                {
                    reset_usb_boot(0, 0);
                }
                boot_mode::set(command == webapp_protocol::SYSTEM_ESP32_FLASH   ? boot_mode::Mode::ESP32_FLASH
                             : command == webapp_protocol::SYSTEM_ESP32_MONITOR ? boot_mode::Mode::ESP32_MONITOR
                                                               : boot_mode::Mode::NORMAL);
                board_api::reboot();
                return;
            }

            default:
                // write_response(PacketID::RESP_ERROR);
                return;
        }
    } 
    else
    {
        send_live_input(idx, gamepad);
    }
}

/* Custom: the newest input at most every kLiveIntervalMs (the last state is always sent, also
 * when nothing new follows); the touchpad when it changes, or every kTouchRefreshMs. */
void WebAppDevice::send_live_input(uint8_t idx, Gamepad& gamepad)
{
    if (idx >= MAX_GAMEPADS)
        return;
    if (gamepad.new_pad_in())
    {
        live_in_[idx] = gamepad.get_pad_in();
        live_pending_[idx] = true;
    }
    const uint32_t now = board_api::ms_since_boot();
    if (!live_pending_[idx] || now - live_sent_ms_[idx] < kLiveIntervalMs)
        return;
    const Gamepad::PadIn& gp_in = live_in_[idx];
    if (!write_gamepad(idx, gp_in))
        return;
    live_pending_[idx] = false;
    live_sent_ms_[idx] = now;
    if (gp_in.has_motion())
        write_motion(idx, gp_in);  // a full transmit FIFO skips it: the next pass has newer motion
    if (!gp_in.touchpad_valid)
        return;
    uint8_t touch[9];
    std::memcpy(touch, gp_in.touch_raw, 8);
    touch[8] = gp_in.touchpad_click;
    if (std::memcmp(touch, touch_sent_[idx], sizeof(touch)) != 0 || now - touch_sent_ms_[idx] >= kTouchRefreshMs)
    {
        if (write_touch(idx, gp_in))
        {
            std::memcpy(touch_sent_[idx], touch, sizeof(touch));
            touch_sent_ms_[idx] = now;
        }
    }
}

/* Custom: when the page opens the port, drop what is left in the transmit FIFO (the tail of a
 * packet cut when the port last closed), so the page starts on a packet boundary. */
void WebAppDevice::line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)rts;
    if (dtr)
    {
        diag::web_app_opened();  // this boot's session is a web app one (Diagnostics.h)
        tud_cdc_n_write_clear(itf);
        for (uint8_t i = 0; i < MAX_GAMEPADS; ++i)
            live_pending_[i] = true;  // send the current state right away
    }
}

uint16_t WebAppDevice::get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) 
{
    return reqlen;
}

void WebAppDevice::set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {}

bool WebAppDevice::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) 
{
    return false;
}

const uint16_t * WebAppDevice::get_descriptor_string_cb(uint8_t index, uint16_t langid) 
{
    static uint16_t desc_str[32 + 1];
    size_t char_count = 0;

    switch(index)
    {
        case 0:
            std::memcpy(&desc_str[1], CDCDesc::DESC_STRING[0], 2);
            char_count = 1;
            break;

        case 3:
            char_count = board_usb_get_serial(&desc_str[1], 32);
            break;

        default:
            if (index >= sizeof(CDCDesc::DESC_STRING) / sizeof(CDCDesc::DESC_STRING[0]))
            {
                return nullptr;
            }
            const char *str = reinterpret_cast<const char *>(CDCDesc::DESC_STRING[index]);
            char_count = std::strlen(str);
            const size_t max_count = sizeof(desc_str) / sizeof(desc_str[0]) - 1;    
            if (char_count > max_count)
            {
                char_count = max_count;
            }

            for (size_t i = 0; i < char_count; i++)
            {
                desc_str[1 + i] = str[i];
            }
            break;
    }

    desc_str[0] = static_cast<uint16_t>((TUSB_DESC_STRING << 8) | (2 * char_count + 2));
    return desc_str;
}

const uint8_t * WebAppDevice::get_descriptor_device_cb() 
{
    return reinterpret_cast<const uint8_t*>(&CDCDesc::DESC_DEVICE);
}

const uint8_t * WebAppDevice::get_hid_descriptor_report_cb(uint8_t itf) 
{
    return nullptr;
}

const uint8_t * WebAppDevice::get_descriptor_configuration_cb(uint8_t index) 
{
    return CDCDesc::DESC_CONFIG;
}

const uint8_t * WebAppDevice::get_descriptor_device_qualifier_cb() 
{
    return nullptr;
}