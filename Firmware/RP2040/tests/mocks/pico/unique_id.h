// Host mock of the Pico SDK board unique ID (fixed value, settable by tests).
#pragma once

#include <cstdint>
#include <cstring>

#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8

typedef struct {
    uint8_t id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
} pico_unique_board_id_t;

namespace mock_unique_id {
inline uint8_t* value() {
    static uint8_t id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES] = {0xE6, 0x61, 0xB6, 0xDB, 0x16, 0x1A, 0xEE, 0x8D};  // the tested dongle: MAC b6:db:16:1a:ee:8d
    return id;
}
}  // namespace mock_unique_id

static inline void pico_get_unique_board_id(pico_unique_board_id_t* out) {
    std::memcpy(out->id, mock_unique_id::value(), PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
}
