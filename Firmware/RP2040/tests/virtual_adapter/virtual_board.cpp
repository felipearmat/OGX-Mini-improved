#include "virtual_board.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

#include "Board/LogRing.h"
#include "hardware/flash.h"
#include "tusb.h"

namespace virtual_board {

namespace {

std::string s_flash_file;
std::deque<uint8_t> s_rx;
std::vector<uint8_t> s_tx;
bool s_running = true;
bool s_dtr = true;          // no control channel: the port is open from the start
int s_control = -1;         // fd 3 when the harness passes one
std::string s_control_line;
int s_mode_request = -1;
bool s_fresh_flash = true;
int s_flash_ops = 0;
const auto s_start = std::chrono::steady_clock::now();

void save_flash()
{
    const auto& stats = mock_flash::stats();
    const int ops = stats.erases + stats.programs;
    if (ops == s_flash_ops)
        return;
    s_flash_ops = ops;
    std::ofstream out(s_flash_file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(mock_flash::memory()), PICO_FLASH_SIZE_BYTES);
}

} // namespace

void start(const char* flash_file)
{
    const char* env = std::getenv("OGXM_CONTROL_FD");
    const int control = env ? std::atoi(env) : kControlFd;
    if (fcntl(control, F_GETFD) != -1)  // before any file of ours can take fd 3
    {
        s_control = control;
        s_dtr = false;  // until the page opens the port
    }
    s_flash_file = flash_file;
    std::memset(mock_flash::memory(), 0xFF, PICO_FLASH_SIZE_BYTES);  // erased NOR flash
    std::ifstream in(s_flash_file, std::ios::binary);
    if (in)
    {
        in.read(reinterpret_cast<char*>(mock_flash::memory()), PICO_FLASH_SIZE_BYTES);
        s_fresh_flash = false;
    }
    const auto& stats = mock_flash::stats();
    s_flash_ops = stats.erases + stats.programs;
}

namespace {

void control_command(const std::string& line)
{
    if (line == "D1")
        s_dtr = true;
    else if (line == "D0")
    {
        s_dtr = false;
        s_rx.clear();
        s_tx.clear();
    }
    else if (line.size() > 1 && line[0] == 'L')
    {
        const std::string text = line.substr(1) + "\n";
        log_ring_append("esp", text.data(), text.size());  // as the hybrid keeps the ESP32's output
    }
    else if (line.size() > 1 && line[0] == 'M')
        s_mode_request = std::atoi(line.c_str() + 1);
    else
        std::fprintf(stderr, "virtual_adapter: unknown control command '%s'\n", line.c_str());
}

void pump_control()
{
    pollfd fd{s_control, POLLIN, 0};
    while (s_control >= 0 && poll(&fd, 1, 0) > 0)
    {
        char buffer[64];
        const ssize_t n = read(s_control, buffer, sizeof(buffer));
        if (n <= 0)
        {
            s_running = false;  // the harness went away
            s_control = -1;
            break;
        }
        for (ssize_t i = 0; i < n; ++i)
        {
            if (buffer[i] != '\n')
                s_control_line += buffer[i];
            else
            {
                control_command(s_control_line);
                s_control_line.clear();
            }
        }
    }
}

} // namespace

void pump()
{
    pump_control();
    pollfd fd{STDIN_FILENO, POLLIN, 0};
    while (s_running && poll(&fd, 1, 0) > 0)
    {
        uint8_t buffer[512];
        const ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (n <= 0)
        {
            s_running = false;  // the page closed the port
            break;
        }
        if (s_dtr)
            s_rx.insert(s_rx.end(), buffer, buffer + n);  // a closed port receives nothing
    }
    tx_flush();
    save_flash();
    if (s_running && s_rx.empty())
    {
        pollfd idle[2] = {{STDIN_FILENO, POLLIN, 0}, {s_control, POLLIN, 0}};
        poll(idle, s_control >= 0 ? 2 : 1, 1);  // idle: wait up to 1 ms for the page
    }
}

bool running() { return s_running; }

bool dtr() { return s_dtr; }

bool fresh_flash() { return s_fresh_flash; }

int take_mode_request()
{
    const int mode = s_mode_request;
    s_mode_request = -1;
    return mode;
}

void control_write(const char* line)
{
    if (s_control < 0)
        return;
    const size_t len = std::strlen(line);
    if (write(s_control, line, len) != static_cast<ssize_t>(len) || write(s_control, "\n", 1) != 1)
        std::fprintf(stderr, "virtual_adapter: control channel write failed\n");
}

int finish()
{
    tx_flush();
    save_flash();
    return 0;
}

void exit_with(int code)
{
    tx_flush();
    save_flash();
    std::fflush(stdout);
    std::_Exit(code);
}

size_t rx_available() { return s_rx.size(); }

size_t rx_read(void* buffer, size_t len)
{
    const size_t n = len < s_rx.size() ? len : s_rx.size();
    auto* out = static_cast<uint8_t*>(buffer);
    for (size_t i = 0; i < n; ++i)
    {
        out[i] = s_rx.front();
        s_rx.pop_front();
    }
    return n;
}

size_t tx_write(const void* buffer, size_t len)
{
    const auto* in = static_cast<const uint8_t*>(buffer);
    s_tx.insert(s_tx.end(), in, in + len);
    return len;
}

void tx_flush()
{
    if (!s_dtr)
        s_tx.clear();  // nobody reads a closed port
    if (s_tx.empty())
        return;
    size_t done = 0;
    while (done < s_tx.size())
    {
        const ssize_t n = write(STDOUT_FILENO, s_tx.data() + done, s_tx.size() - done);
        if (n <= 0)
            break;
        done += static_cast<size_t>(n);
    }
    s_tx.clear();
}

void tx_clear() { s_tx.clear(); }

uint32_t ms() { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - s_start).count()); }

} // namespace virtual_board

// ---- TinyUSB: the CDC device API the web app driver uses ----

extern "C" {

bool tud_cdc_n_connected(uint8_t) { return virtual_board::running() && virtual_board::dtr(); }
uint32_t tud_cdc_n_available(uint8_t) { return static_cast<uint32_t>(virtual_board::rx_available()); }
uint32_t tud_cdc_n_read(uint8_t, void* buffer, uint32_t bufsize)
{
    return static_cast<uint32_t>(virtual_board::rx_read(buffer, bufsize));
}
uint32_t tud_cdc_n_write(uint8_t, void const* buffer, uint32_t bufsize)
{
    return static_cast<uint32_t>(virtual_board::tx_write(buffer, bufsize));
}
uint32_t tud_cdc_n_write_flush(uint8_t)
{
    virtual_board::tx_flush();
    return 0;
}
uint32_t tud_cdc_n_write_available(uint8_t) { return 4096; }
bool tud_cdc_n_write_clear(uint8_t)
{
    virtual_board::tx_clear();
    return true;
}
void tud_task_ext(uint32_t, bool) { virtual_board::pump(); }
bool tud_disconnect(void) { return true; }
bool tud_connect(void) { return true; }
bool tud_mounted(void) { return true; }
bool tud_suspended(void) { return false; }
bool tud_remote_wakeup(void) { return false; }

// bsp/board_api.h
size_t board_usb_get_serial(uint16_t desc_str[], size_t max_chars)
{
    static const char serial[] = "VIRTUAL0001";
    size_t n = 0;
    for (; serial[n] && n < max_chars; ++n)
        desc_str[n] = static_cast<uint16_t>(serial[n]);
    return n;
}

// pico/bootrom.h
void reset_usb_boot(uint32_t, uint32_t) { virtual_board::exit_with(4); }

// CDC class driver entry points referenced by WebAppDevice::initialize() (never called here)
void cdcd_init(void) {}
bool cdcd_deinit(void) { return true; }
void cdcd_reset(uint8_t) {}
uint16_t cdcd_open(uint8_t, tusb_desc_interface_t const*, uint16_t) { return 0; }
bool cdcd_control_xfer_cb(uint8_t, uint8_t, tusb_control_request_t const*) { return false; }
bool cdcd_xfer_cb(uint8_t, uint8_t, xfer_result_t, uint32_t) { return true; }

} // extern "C"

// ---- Pico SDK and board API ----

uint64_t mock_time_us = 0;  // pico/time.h (tests/mocks)

void sleep_ms(uint32_t ms)
{
    const uint32_t until = virtual_board::ms() + ms;
    while (static_cast<int32_t>(virtual_board::ms() - until) < 0)
        virtual_board::pump();
}

void busy_wait_ms(uint32_t ms) { sleep_ms(ms); }

namespace board_api {

void init_board() {}
void init_bluetooth() {}
void reboot() { virtual_board::exit_with(3); }  // the harness restarts the process on the same flash file
void set_led(bool) {}
void set_led_color(uint8_t, uint8_t, uint8_t) {}
uint32_t ms_since_boot()
{
    mock_time_us = static_cast<uint64_t>(virtual_board::ms()) * 1000u;
    return virtual_board::ms();
}

namespace usb {
bool host_connected() { return false; }
void disconnect_all() { virtual_board::tx_flush(); }
bool host_any_pad_mounted() { return false; }
} // namespace usb

} // namespace board_api
