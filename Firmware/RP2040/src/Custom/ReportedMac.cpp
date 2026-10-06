#include "Custom/ReportedMac.h"

#include <cstring>

#include "pico/critical_section.h"
#include "Custom/BoardMac.h"
#include "Custom/DongleSettings.h"

namespace reported_mac {

namespace {

critical_section_t& lock()
{
    static critical_section_t cs;
    static bool initialized = false;
    if (!initialized) {
        /* Shared ("striped") spin lock: the claimable ones are all in use (test_spin_lock_budget). */
        critical_section_init_with_lock_num(&cs, next_striped_spin_lock_num());
        initialized = true;
    }
    return cs;
}

uint8_t s_pad_mac[6]{};          // LSB first
bool s_pad_known = false;
uint8_t s_reported_mac[6]{};     // LSB first
bool s_reported = false;
bool s_reconnect = false;

} // namespace

void on_pad_ready(const uint8_t bd_addr[6])
{
    if (!dongle_settings::get().mac_per_controller)
        return;
    critical_section_enter_blocking(&lock());
    if (!s_pad_known) {
        for (int i = 0; i < 6; ++i)
            s_pad_mac[i] = bd_addr[5 - i];
        s_pad_known = true;
        // The host read another address at enumeration: have it enumerate again.
        if (s_reported && std::memcmp(s_reported_mac, s_pad_mac, sizeof(s_pad_mac)) != 0)
            s_reconnect = true;
    }
    critical_section_exit(&lock());
}

void get_lsb_first(uint8_t mac[6])
{
    critical_section_enter_blocking(&lock());
    if (s_pad_known)
        std::memcpy(mac, s_pad_mac, 6);
    else
        board_mac::get_lsb_first(mac);
    std::memcpy(s_reported_mac, mac, 6);
    s_reported = true;
    critical_section_exit(&lock());
}

void init()
{
    (void)lock();
}

bool take_reconnect_request()
{
    critical_section_enter_blocking(&lock());
    const bool r = s_reconnect;
    s_reconnect = false;
    critical_section_exit(&lock());
    return r;
}

} // namespace reported_mac
