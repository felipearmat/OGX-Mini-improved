// Boot blink code and lightbar colour per output mode.
#include "Custom/ModeIndicator.h"
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
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::DINPUT), 3);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::PS4), 4);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::STEAM), 5);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::XBOXOG), 6);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::XBOXOG_SB), 6);
    CHECK_EQ(mode_indicator::blink_count(DeviceDriverType::PS3), 7);
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
    const DeviceDriverType modes[] = {DeviceDriverType::XINPUT, DeviceDriverType::SWITCH,
                                      DeviceDriverType::DINPUT, DeviceDriverType::PS4,
                                      DeviceDriverType::STEAM,  DeviceDriverType::XBOXOG,
                                      DeviceDriverType::PS3,    DeviceDriverType::WIIU};
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

TEST(led_colour_full_brightness_is_lightbar_colour) {
    uint8_t lr, lg, lb, r, g, b;
    mode_indicator::lightbar_color(DeviceDriverType::XBOXOG, lr, lg, lb);
    mode_indicator::led_color(DeviceDriverType::XBOXOG, 255, r, g, b);
    CHECK_EQ(r, lr);
    CHECK_EQ(g, lg);
    CHECK_EQ(b, lb);
}

TEST(led_colour_scales_and_keeps_lit_channels_visible) {
    uint8_t r, g, b;
    mode_indicator::led_color(DeviceDriverType::XINPUT, 51, r, g, b);  // 20 %
    CHECK_EQ(r, 0);
    CHECK_EQ(g, 51);
    CHECK_EQ(b, 0);
    // PS3 dark blue (0x50) would round to 0 at very low brightness.
    mode_indicator::led_color(DeviceDriverType::PS3, 2, r, g, b);
    CHECK_EQ(r, 0);
    CHECK_EQ(g, 0);
    CHECK_EQ(b, 1);
    mode_indicator::led_color(DeviceDriverType::PS3, 0, r, g, b);
    CHECK_EQ(b, 0);
}

TEST(led_colours_stay_distinct_at_default_brightness) {
    const DeviceDriverType modes[] = {DeviceDriverType::XINPUT, DeviceDriverType::SWITCH,
                                      DeviceDriverType::DINPUT, DeviceDriverType::PS4,
                                      DeviceDriverType::STEAM,  DeviceDriverType::XBOXOG,
                                      DeviceDriverType::PS3,    DeviceDriverType::WIIU};
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

TEST_MAIN()
