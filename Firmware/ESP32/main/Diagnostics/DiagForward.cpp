#include "Diagnostics/DiagForward.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "sdkconfig.h"
#include "Board/ogxm_log.h"
#include "Diagnostics/Diagnostics.h"

using Esp32Bridge::PacketDiag;

namespace {

diag_forward::Sender s_send = nullptr;

struct Counter
{
    bool fresh{false};
    uint8_t value{0}, bits{0}, unit{0};
    // the ESP32's own loss count (controllers with a known counter step)
    bool seen{false};
    uint8_t last{0};
    uint32_t received{0}, lost{0};
    bool lost_valid{false};
};
Counter s_counters[diag::kSlots];
uint32_t s_le_adv_reports = 0;

/* Writes little-endian fields into a packet's data. */
struct Writer
{
    PacketDiag packet;
    size_t len{0};

    Writer(uint8_t kind, size_t slot = 0)
    {
        packet.kind = kind;
        packet.slot = static_cast<uint8_t>(slot);
    }
    Writer& u8(uint8_t v)
    {
        if (len < PacketDiag::kData) packet.data[len++] = v;
        return *this;
    }
    Writer& u16(uint16_t v) { return u8(v & 0xFF).u8(v >> 8); }
    Writer& bytes(const void* data, size_t n)
    {
        const size_t room = PacketDiag::kData - len;
        n = std::min(n, room);
        std::memcpy(packet.data + len, data, n);
        len += n;
        return *this;
    }
    void send()
    {
        if (s_send) s_send(packet);
    }
};

} // namespace

namespace diag_forward {

void set_sender(Sender send)
{
    s_send = send;
}

bool take_counter(int slot, uint8_t& value, uint8_t& bits, uint8_t& unit)
{
    if (slot < 0 || slot >= static_cast<int>(diag::kSlots) || !s_counters[slot].fresh)
        return false;
    Counter& c = s_counters[slot];
    c.fresh = false;
    value = c.value;
    bits = c.bits;
    unit = c.unit;
    return true;
}

void take_slot_stats(int slot, uint32_t& received, uint32_t& lost, bool& lost_valid)
{
    received = lost = 0;
    lost_valid = false;
    if (slot < 0 || slot >= static_cast<int>(diag::kSlots))
        return;
    Counter& c = s_counters[slot];
    received = c.received;
    lost = c.lost;
    lost_valid = c.lost_valid;
    c.received = c.lost = 0;
}

void tick()
{
    if (s_le_adv_reports) {
        Writer(PacketDiag::LE_ADV_REPORTS).u16(static_cast<uint16_t>(std::min<uint32_t>(s_le_adv_reports, 0xFFFF))).send();
        s_le_adv_reports = 0;
    }
}

} // namespace diag_forward

namespace diag {

/* An event line, also logged here; sent in parts (the report keeps kEventText characters). */
void event(uint32_t now_ms, const char* fmt, ...)
{
    (void)now_ms;  // the RP2040 stamps it on arrival
    char text[kEventText];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    OGXM_LOG("BP32: %s\n", text);

    const size_t len = std::strlen(text);
    size_t sent = 0;
    do {
        uint8_t part[PacketDiag::kData]{};
        const size_t n = std::min(len - sent, PacketDiag::kEventPart);
        std::memcpy(&part[1], text + sent, n);
        sent += n;
        part[0] = sent >= len ? 1 : 0;
        Writer(PacketDiag::EVENT).bytes(part, sizeof(part)).send();
    } while (sent < len);
}

void slot_connected(size_t slot, uint32_t now_ms, const char* name, uint16_t vid, uint16_t pid,
                    uint8_t controller_type, bool le, uint16_t con_handle, const uint8_t address[6])
{
    (void)now_ms;
    Writer(PacketDiag::SLOT_NAME, slot).bytes(name, strnlen(name, PacketDiag::kData)).send();
    Writer(PacketDiag::SLOT_READY, slot).u16(vid).u16(pid).u8(controller_type).u8(le ? 1 : 0)
        .u16(con_handle).bytes(address, 6).send();
}

void companion_slot(size_t slot, uint16_t reports_per_s, bool lost_valid, uint16_t lost_x10)
{
    Writer(PacketDiag::COMPANION_SLOT, slot).u16(reports_per_s).u8(lost_valid ? 1 : 0).u16(lost_x10).send();
}

void companion_info(uint32_t now_ms, const char* firmware, uint32_t uptime_s, bool high_tx_power,
                    uint32_t i2c_write_errors, uint32_t i2c_read_errors, uint32_t queue_drops)
{
    (void)now_ms;
    auto sat = [](uint32_t v) { return static_cast<uint16_t>(v > 0xFFFF ? 0xFFFF : v); };
    Writer(PacketDiag::COMPANION_INFO).u16(static_cast<uint16_t>(uptime_s)).u16(static_cast<uint16_t>(uptime_s >> 16))
        .u8(high_tx_power ? 1 : 0).u16(sat(i2c_write_errors)).u16(sat(i2c_read_errors)).u16(sat(queue_drops))
        .bytes(firmware, strnlen(firmware, PacketDiag::kData - 11)).send();
}

void companion_version(const char* version)
{
    Writer(PacketDiag::COMPANION_VERSION).bytes(version, strnlen(version, PacketDiag::kData)).send();
}

void slot_disconnected(size_t slot, uint32_t now_ms)
{
    (void)now_ms;
    if (slot < kSlots) s_counters[slot] = Counter{};
    Writer(PacketDiag::SLOT_GONE, slot).send();
}

void slot_report(size_t slot, uint32_t now_ms)
{
    // Measured on the RP2040 from SET_PAD (PacketIn::bt_slot), one per report.
    (void)slot;
    (void)now_ms;
}

void slot_counter(size_t slot, uint32_t value, uint8_t bits, uint8_t unit)
{
    if (slot >= kSlots) return;
    Counter& c = s_counters[slot];
    c.fresh = true;
    c.value = static_cast<uint8_t>(value);
    c.bits = bits;
    c.unit = unit;
    /* The ESP32's own loss count, where the step per report is fixed (DS4 / DualSense: 1). */
    if (unit > 0 && bits > 0 && bits <= 8) {
        const uint8_t mask = static_cast<uint8_t>((1u << bits) - 1u);
        if (c.seen) {
            const uint8_t steps = static_cast<uint8_t>(((c.value - c.last) & mask) / unit);
            if (steps > 0) {
                ++c.received;
                c.lost += steps - 1u;
            }
        } else {
            ++c.received;
        }
        c.seen = true;
        c.lost_valid = true;
    }
    c.last = c.value;
}

void slot_battery(size_t slot, uint8_t level_0_255)
{
    Writer(PacketDiag::BATTERY, slot).u8(level_0_255).send();
}

void slot_switch_firmware(size_t slot, uint8_t major, uint8_t minor)
{
    static uint16_t s_sent[kSlots]{};
    const uint16_t version = static_cast<uint16_t>(0x8000 | (major << 8) | minor);
    if (slot >= kSlots || s_sent[slot] == version) return;  // asked every pass: send on change
    s_sent[slot] = version;
    Writer(PacketDiag::SWITCH_FIRMWARE, slot).u8(major).u8(minor).send();
}

void le_parameters(uint16_t con_handle, uint16_t interval, uint16_t latency, uint16_t timeout)
{
    Writer(PacketDiag::LE_PARAMETERS).u16(con_handle).u16(interval).u16(latency).u16(timeout).send();
}

void remote_version(uint16_t con_handle, uint8_t bt_version, uint16_t company_id, uint16_t subversion)
{
    Writer(PacketDiag::REMOTE_VERSION).u16(con_handle).u8(bt_version).u16(company_id).u16(subversion).send();
}

void device_information(uint16_t con_handle, const char* field, const char* value)
{
    static const char* const kFields[] = {"manufacturer", "model", "firmware", "hardware", "software"};
    uint8_t index = 0;
    while (index < 5 && std::strcmp(kFields[index], field) != 0) ++index;
    if (index >= 5) return;
    Writer(PacketDiag::DEVICE_INFO).u16(con_handle).u8(index).bytes(value, strnlen(value, PacketDiag::kData - 3)).send();
}

void pnp_id(uint16_t con_handle, uint8_t source, uint16_t vid, uint16_t pid, uint16_t version)
{
    Writer(PacketDiag::PNP_ID).u16(con_handle).u8(source).u16(vid).u16(pid).u16(version).send();
}

void rssi(uint16_t con_handle, int8_t dbm)
{
    Writer(PacketDiag::RSSI).u16(con_handle).u8(static_cast<uint8_t>(dbm)).send();
}

void channels(uint16_t con_handle, uint8_t used, uint8_t total)
{
    Writer(PacketDiag::CHANNELS).u16(con_handle).u8(used).u8(total).send();
}

void failed_contacts(uint16_t con_handle, uint16_t count)
{
    Writer(PacketDiag::FAILED_CONTACTS).u16(con_handle).u16(count).send();
}

void tx_power(uint16_t con_handle, bool maximum, int8_t dbm)
{
    Writer(PacketDiag::TX_POWER).u16(con_handle).u8(maximum ? 1 : 0).u8(static_cast<uint8_t>(dbm)).send();
}

void link_mode(uint16_t con_handle, uint8_t mode, uint16_t interval_slots)
{
    Writer(PacketDiag::LINK_MODE).u16(con_handle).u8(mode).u16(interval_slots).send();
}

void searching(bool accepting_new_controllers)
{
    static int s_sent = -1;  // asked every pass: send on change
    if (s_sent == (accepting_new_controllers ? 1 : 0)) return;
    s_sent = accepting_new_controllers ? 1 : 0;
    Writer(PacketDiag::SEARCHING).u8(accepting_new_controllers ? 1 : 0).send();
}

void inquiry_complete(uint32_t now_ms)
{
    (void)now_ms;
    Writer(PacketDiag::INQUIRY_COMPLETE).send();
}

void le_adv_report()
{
    ++s_le_adv_reports;
}

} // namespace diag
