// The board under the virtual adapter: the USB serial port on stdin / stdout and the flash in a
// file (tests/mocks/hardware/flash.h keeps it in RAM; this saves it after every change).
//
// Optional control channel on fd 3 (or the fd in OGXM_CONTROL_FD), one command per line, for what
// is not serial data:
//   D1 / D0   the page opened / closed the port (DTR). Without fd 3 the port is open from the start.
//   M<n>      the mode combo for output mode n was held (ModeCombos.h)
//   L<text>   a line of the ESP32's serial output, into the kept log (Board/LogRing.h)
// and from the adapter, at boot:
//   MODE <n>  the output mode it runs in; the serial port exists in D-Input/Debug (6) only.
//   LED <rrggbb>  the external status LED's colour, when it changes (D-Input/Debug).
#pragma once

#include <cstddef>
#include <cstdint>

namespace virtual_board {

    void start(const char* flash_file);
    // Moves bytes between stdin / stdout and the serial port's buffers, saves the flash if it changed.
    void pump();
    bool running();
    // The page has the port open (DTR).
    bool dtr();

    constexpr int kControlFd = 3;
    // No flash file yet: a new board.
    bool fresh_flash();
    // The mode a combo asked for (M<n>), or -1; once.
    int take_mode_request();
    // A line to the harness (no-op without the control channel).
    void control_write(const char* line);
    int finish();
    [[noreturn]] void exit_with(int code);

    // The serial port (tud_cdc_* in virtual_board.cpp use these).
    size_t rx_available();
    size_t rx_read(void* buffer, size_t len);
    size_t tx_write(const void* buffer, size_t len);
    void tx_flush();
    void tx_clear();

    // Milliseconds since the process started (board_api::ms_since_boot).
    uint32_t ms();

} // namespace virtual_board
