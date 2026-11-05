// Boot blink code and lightbar colour per output mode; the shared status LED.
#include <vector>

#include "Board/ModeIndicator.h"
#include "Board/StatusLed.h"
#include "test.h"

namespace {

// Plays the whole code and returns how many "LED on" steps it had.
int count_blinks(DeviceDriverType mode) {
    mode_indicator::begin(mode);
    int on_steps = 0;
    bool led_on = false;
    uint32_t ms = 0;
    for (int guard = 0; guard < 1000 && mode_indicator::next_step(led_on, ms); ++guard) {
        CHECK(ms > 0);
        on_steps += led_on ? 1 : 0;
    }
    CHECK(!mode_indicator::active());
    return on_steps;
}

}  // namespace

TEST(blink_count_per_mode) {
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::XINPUT), 1);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::SWITCH), 2);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::DINPUT_DEBUG), 3);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::PS4), 4);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::STEAM), 5);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::XBOXOG), 6);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::XBOXOG_SB), 6);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::PS3), 7);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::KBM), 8);
}

TEST(code_is_played_twice_then_stops) {
    CHECK_EQ(count_blinks(DeviceDriverType::XINPUT), 2);
    CHECK_EQ(count_blinks(DeviceDriverType::SWITCH), 4);
    CHECK_EQ(count_blinks(DeviceDriverType::XBOXOG), 12);
    bool led_on = true;
    uint32_t ms = 0;
    CHECK(!mode_indicator::next_step(led_on, ms));
}

TEST(each_repetition_ends_with_led_off_pause) {
    mode_indicator::begin(DeviceDriverType::SWITCH);
    bool led_on = false;
    uint32_t ms = 0;
    // 2 blinks = on, off, on, off, then the pause.
    for (int i = 0; i < 4; ++i)
        mode_indicator::next_step(led_on, ms);
    mode_indicator::next_step(led_on, ms);
    CHECK(!led_on);
    CHECK(ms >= 1000);
}

TEST(lightbar_colours_are_distinct) {
    // One mode per colour (GameCube = N64, PS1/PS2 = PS Classic, the Xbox variants).
    const DeviceDriverType modes[] = {DeviceDriverType::SWITCH,    DeviceDriverType::WIIU,
                                      DeviceDriverType::WII,       DeviceDriverType::GAMECUBE,
                                      DeviceDriverType::XINPUT,    DeviceDriverType::XBOXOG,
                                      DeviceDriverType::PS1PS2,    DeviceDriverType::PS3,
                                      DeviceDriverType::PS4,       DeviceDriverType::STEAM,
                                      DeviceDriverType::KBM,       DeviceDriverType::DREAMCAST,
                                      DeviceDriverType::DINPUT_DEBUG};
    constexpr int kModes = sizeof(modes) / sizeof(modes[0]);
    uint32_t colours[kModes];
    for (int i = 0; i < kModes; ++i) {
        uint8_t r, g, b;
        mode_indicator::lightbar_color(modes[i], r, g, b);
        colours[i] = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
        CHECK(colours[i] != 0);
    }
    for (int i = 0; i < kModes; ++i)
        for (int j = i + 1; j < kModes; ++j)
            CHECK(colours[i] != colours[j]);
}

TEST(led_colour_scales_and_keeps_lit_channels_visible) {
    uint8_t r, g, b;
    mode_indicator::led_color(DeviceDriverType::XINPUT, 51, r, g, b);  // 20 %
    CHECK_EQ(r, 0);
    CHECK_EQ(g, 51);
    CHECK_EQ(b, 0);
    // The light green's 0xA0 channels would round to 0 at the lowest brightness.
    mode_indicator::led_color(DeviceDriverType::XBOXOG, 1, r, g, b);
    CHECK_EQ(r, 1);
    CHECK_EQ(g, 1);
    CHECK_EQ(b, 1);
    mode_indicator::led_color(DeviceDriverType::XBOXOG, 0, r, g, b);
    CHECK_EQ(r, 0);
}

TEST(led_colours_stay_distinct_at_default_brightness) {
    // One mode per colour (GameCube = N64, PS1/PS2 = PS Classic, the Xbox variants).
    const DeviceDriverType modes[] = {DeviceDriverType::SWITCH,    DeviceDriverType::WIIU,
                                      DeviceDriverType::WII,       DeviceDriverType::GAMECUBE,
                                      DeviceDriverType::XINPUT,    DeviceDriverType::XBOXOG,
                                      DeviceDriverType::PS1PS2,    DeviceDriverType::PS3,
                                      DeviceDriverType::PS4,       DeviceDriverType::STEAM,
                                      DeviceDriverType::KBM,       DeviceDriverType::DREAMCAST,
                                      DeviceDriverType::DINPUT_DEBUG};
    constexpr int kModes = sizeof(modes) / sizeof(modes[0]);
    uint32_t colours[kModes];
    for (int i = 0; i < kModes; ++i) {
        uint8_t r, g, b;
        mode_indicator::led_color(modes[i], 48, r, g, b);
        colours[i] = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
        CHECK(colours[i] != 0);
    }
    for (int i = 0; i < kModes; ++i)
        for (int j = i + 1; j < kModes; ++j)
            CHECK(colours[i] != colours[j]);
}

TEST(status_led_plays_the_code_then_blinks_while_searching) {
    using status_led::Search;
    status_led::Blinker led;
    bool on = false;
    uint32_t ms = 0;
    mode_indicator::begin(DeviceDriverType::XINPUT);
    int code_steps = 0;
    while (mode_indicator::active()) {
        CHECK(led.step(Search::FULL, on, ms));  // every code step is written
        ++code_steps;
    }
    CHECK_EQ(code_steps, 6);  // (on, off, pause) twice

    CHECK(led.step(Search::FULL, on, ms));
    const bool first = on;
    CHECK_EQ(ms, status_led::kFastBlinkMs);
    CHECK(led.step(Search::FULL, on, ms));
    CHECK(on != first);
    CHECK(led.step(Search::REDUCED, on, ms));
    CHECK_EQ(ms, status_led::kSlowBlinkMs);
}

TEST(status_led_is_solid_without_search_and_written_once) {
    using status_led::Search;
    status_led::Blinker led;
    bool on = false;
    uint32_t ms = 0;
    CHECK(led.step(Search::NONE, on, ms));
    CHECK(on);
    CHECK(!led.step(Search::NONE, on, ms));  // unchanged: no write
    CHECK(on);
    CHECK(!led.step(Search::NONE, on, ms));
    // Search back on: blinks again from the next change.
    bool changed = false;
    for (int i = 0; i < 2; ++i) changed |= led.step(Search::FULL, on, ms);
    CHECK(changed);
}

TEST(custom_colours_override_and_generation) {
    uint8_t r, g, b;
    mode_indicator::ModeColors colors{};
    mode_indicator::set_custom_colors(colors);
    const uint32_t gen0 = mode_indicator::color_generation();
    // all zero: the defaults
    mode_indicator::shown_color(DeviceDriverType::SWITCH, r, g, b);
    CHECK_EQ(r, 0xFF); CHECK_EQ(g, 0); CHECK_EQ(b, 0);
    // a colour for one mode; the web app mode uses DInput's slot
    colors.rgb[static_cast<int>(DeviceDriverType::SWITCH)][1] = 0x80;
    colors.rgb[static_cast<int>(DeviceDriverType::DINPUT_DEBUG)][2] = 0x40;
    mode_indicator::set_custom_colors(colors);
    CHECK(mode_indicator::color_generation() != gen0);
    mode_indicator::shown_color(DeviceDriverType::SWITCH, r, g, b);
    CHECK_EQ(r, 0); CHECK_EQ(g, 0x80); CHECK_EQ(b, 0);
    mode_indicator::shown_color(DeviceDriverType::WEBAPP, r, g, b);
    CHECK_EQ(b, 0x40);
    mode_indicator::shown_color(DeviceDriverType::XINPUT, r, g, b);  // untouched: default green
    CHECK_EQ(g, 0xFF);
    // the preview replaces every mode's colour until turned off
    const uint32_t gen1 = mode_indicator::color_generation();
    mode_indicator::set_override(true, 1, 2, 3);
    CHECK(mode_indicator::color_generation() != gen1);
    mode_indicator::shown_color(DeviceDriverType::PS4, r, g, b);
    CHECK_EQ(r, 1); CHECK_EQ(g, 2); CHECK_EQ(b, 3);
    const uint32_t gen2 = mode_indicator::color_generation();
    mode_indicator::set_override(true, 1, 2, 3);  // same colour: no new generation
    CHECK_EQ(mode_indicator::color_generation(), gen2);
    mode_indicator::set_override(false, 0, 0, 0);
    mode_indicator::shown_color(DeviceDriverType::SWITCH, r, g, b);
    CHECK_EQ(g, 0x80);
    mode_indicator::set_custom_colors(mode_indicator::ModeColors{});
}

TEST(mode_colours_record_is_checked) {
    mode_indicator::ModeColors out{};
    uint8_t bytes[sizeof(mode_indicator::ModeColors)]{};
    bytes[0] = mode_indicator::ModeColors::kVersion;
    bytes[1] = 9;
    CHECK(mode_indicator::decode_colors(bytes, sizeof(bytes), out));
    CHECK_EQ(out.rgb[0][0], 9);
    CHECK(!mode_indicator::decode_colors(bytes, sizeof(bytes) - 1, out));
    bytes[0] = 2;
    CHECK(!mode_indicator::decode_colors(bytes, sizeof(bytes), out));
}

TEST_MAIN()

TEST(status_led_battery_alert_five_quick_blinks_then_four_seconds_as_usual) {
    // 2026-11: a Joy-Con ran out of battery mid-session with no warning on the adapter.
    using status_led::Search;
    status_led::Blinker led;
    bool on = false, alert = false;
    uint32_t ms = 0;
    int ons = 0;
    for (int i = 0; i < 2 * status_led::kAlertBlinks; ++i) {
        CHECK(led.step(Search::NONE, true, on, ms, alert));
        CHECK(alert);
        if (on) ++ons;
        CHECK_EQ(ms, on ? status_led::kAlertOnMs : status_led::kAlertOffMs);
    }
    CHECK_EQ(ons, static_cast<int>(status_led::kAlertBlinks));
    // Then as usual (solid, no search) for kAlertEveryMs, then the blinks again.
    uint32_t usual = 0;
    while (true) {
        led.step(Search::NONE, true, on, ms, alert);
        if (alert) break;
        CHECK(on);
        usual += ms;
        CHECK(usual <= status_led::kAlertEveryMs + status_led::kFastBlinkMs);
    }
    CHECK(usual >= status_led::kAlertEveryMs);
    // Charged: no more alerts.
    for (int i = 0; i < 40; ++i) {
        led.step(Search::NONE, false, on, ms, alert);
        CHECK(!alert);
    }
}

TEST(status_led_alert_colour_stays_dim_and_is_written_once) {
    // Red scaled by the brightness to at most kAlertLevel (the same on every board).
    CHECK_EQ(status_led::alert_red(0), 255);
    CHECK_EQ(status_led::alert_red(status_led::kAlertLevel), 255);
    CHECK_EQ(status_led::alert_red(255), static_cast<uint8_t>(255u * status_led::kAlertLevel / 255));
    CHECK(status_led::alert_red(255) * 255u / 255u <= status_led::kAlertLevel);
    status_led::AlertColour c;
    CHECK(!c.changed(false));
    CHECK(c.changed(true));
    CHECK(!c.changed(true));
    CHECK(c.changed(false));
}

TEST(status_led_indicator_shared_by_every_board) {
    // Board/StatusLed.h Indicator: the one LED tick of the Pico W, the hybrid's RP2040 and its ESP32.
    using status_led::Search;
    status_led::Indicator ind;
    std::vector<uint32_t> colours;  // 0xRRGGBB written
    int led_writes = 0, colour_events = 0;
    bool led_on = false;
    auto set_colour = [&](uint8_t r, uint8_t g, uint8_t b) { colours.push_back(uint32_t(r) << 16 | g << 8 | b); };
    auto set_led = [&](bool on) { ++led_writes; led_on = on; };
    auto on_colour = [&]() { ++colour_events; };
    const uint8_t white[3] = {255, 255, 255};
    const uint8_t red[3] = {255, 0, 0};
    const uint32_t opts = status_led::Indicator::pack_options(1, 1, 48);
    // First tick: the mode colour written once, the colour event, the LED written.
    for (int i = 0; i < 40; ++i)  // past the mode's blink code
        ind.tick(Search::NONE, false, white, 1, opts, 48, set_colour, set_led, on_colour);
    CHECK_EQ(colours.size(), 1u);
    CHECK_EQ(colours[0], 0xFFFFFFu);
    CHECK_EQ(colour_events, 1);
    CHECK(led_on);
    // Solid with nothing new: no more writes.
    const int writes = led_writes;
    ind.tick(Search::NONE, false, white, 1, opts, 48, set_colour, set_led, on_colour);
    CHECK_EQ(led_writes, writes);
    // A mode change (new generation): the new colour once, and the LED written again.
    ind.tick(Search::NONE, false, red, 2, opts, 48, set_colour, set_led, on_colour);
    CHECK_EQ(colours.back(), 0xFF0000u);
    CHECK_EQ(colour_events, 2);
    CHECK(led_writes > writes);
    // An LED option change: written again, no colour event.
    const int w2 = led_writes;
    ind.tick(Search::NONE, false, red, 2, status_led::Indicator::pack_options(0, 1, 48), 48, set_colour, set_led, on_colour);
    CHECK(led_writes > w2);
    CHECK_EQ(colour_events, 2);
    // Battery critical: dim red during the alert, then the mode colour back.
    const size_t before = colours.size();
    for (int i = 0; i < 2 * status_led::kAlertBlinks + 2; ++i)
        ind.tick(Search::NONE, true, red, 2, status_led::Indicator::pack_options(0, 1, 48), 48, set_colour, set_led, on_colour);
    CHECK(colours.size() >= before + 2);
    CHECK_EQ(colours[before], uint32_t(status_led::alert_red(48)) << 16);
    CHECK_EQ(colours.back(), 0xFF0000u);
}
