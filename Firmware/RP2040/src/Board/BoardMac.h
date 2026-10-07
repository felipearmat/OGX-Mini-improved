#ifndef _OGXM_CUSTOM_BOARD_MAC_H_
#define _OGXM_CUSTOM_BOARD_MAC_H_

#include <cstdint>

#include "pico/unique_id.h"

/*  A stable MAC address for the emulated DS4 / DualSense pairing-info feature reports (custom
 *  addition). Derived from the board's unique ID, locally administered and unicast, so each
 *  dongle has an address of its own. Written LSB first, as those reports carry it. */
namespace board_mac {

    inline void get_lsb_first(uint8_t mac[6])
    {
        pico_unique_board_id_t id;
        pico_get_unique_board_id(&id);
        for (int i = 0; i < 6; ++i)
            mac[i] = id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1 - i];
        mac[5] = static_cast<uint8_t>((mac[5] | 0x02) & ~0x01);  // MSB: local, unicast
    }

} // namespace board_mac

#endif // _OGXM_CUSTOM_BOARD_MAC_H_
