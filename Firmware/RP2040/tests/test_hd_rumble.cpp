// Decoding of Switch HD rumble blocks sent by the host (USBDevice/DeviceDriver/Switch/HdRumble).
// Block values marked "Steam" were captured from Steam's controller settings over USB.
#include "USBDevice/DeviceDriver/Switch/HdRumble.h"
#include "test.h"

namespace {

struct Block {
    uint8_t b[4];
};

// One 7-bit sample as SDL's EncodeRumble builds it: high-band freq 0x74, low-band freq 0x3D.
Block sdl_block(uint8_t high_amp_byte, uint16_t low_amp_word) {
    return {{0x74, high_amp_byte, static_cast<uint8_t>(0x3D | ((low_amp_word >> 8) & 0x80)),
             static_cast<uint8_t>(low_amp_word & 0xFF)}};
}

uint8_t apply(hd_rumble::Decoder& d, Block blk) { return d.apply(blk.b); }

const Block kNeutral = {{0x00, 0x01, 0x40, 0x40}};
const Block kSteamTick = {{0x00, 0x00, 0x05, 0xC0}};   // Steam: silence, 4% blip, silence
const Block kSteamStrong = {{0x74, 0x88, 0x3D, 0x62}};  // Steam: trigger test rumble

}  // namespace

TEST(neutral_blocks_are_silent) {
    hd_rumble::Decoder d;
    CHECK_EQ(apply(d, kNeutral), 0);
    CHECK_EQ(apply(d, Block{{0x00, 0x00, 0x01, 0x40}}), 0);  // SDL's alternative neutral
    CHECK_EQ(apply(d, sdl_block(0x00, 0x0040)), 0);          // zero amplitude, other freqs
}

TEST(full_scale) {
    hd_rumble::Decoder d;
    CHECK_EQ(apply(d, sdl_block(0xC8, 0x0040)), 255);  // high band only
    CHECK_EQ(apply(d, sdl_block(0x00, 0x0072)), 255);  // low band only
}

TEST(low_band_lsb_does_not_jump_to_full) {
    hd_rumble::Decoder d;
    const uint8_t one = apply(d, sdl_block(0x00, 0x8040));
    const uint8_t two = apply(d, sdl_block(0x00, 0x0041));
    CHECK(one > 0 && one < 10);
    CHECK(two >= one && two < 10);
}

TEST(bands_share_the_amplitude_table) {
    hd_rumble::Decoder d;
    const uint8_t high = apply(d, sdl_block(0x64, 0x0040));
    const uint8_t low = apply(d, sdl_block(0x00, 0x0059));
    CHECK_EQ(high, low);
    CHECK_EQ(apply(d, sdl_block(0x64, 0x0045)), high);
}

TEST(monotonic_in_code) {
    hd_rumble::Decoder d;
    uint8_t prev = 0;
    for (int code = 0; code <= 100; ++code) {
        const uint8_t v = apply(d, sdl_block(static_cast<uint8_t>(code << 1), 0x0040));
        CHECK(v >= prev);
        prev = v;
    }
}

TEST(steam_ui_tick_ends_silent) {
    // It used to decode as a strong rumble that stayed on (no neutral follows it).
    hd_rumble::Decoder d;
    for (int i = 0; i < 20; ++i)
        CHECK_EQ(apply(d, kSteamTick), 0);
    // ...also right after a rumble.
    CHECK(apply(d, kSteamStrong) > 100);
    CHECK_EQ(apply(d, kSteamTick), 0);
}

TEST(steam_trigger_rumble_then_neutral) {
    hd_rumble::Decoder d;
    CHECK(apply(d, kSteamStrong) > 100);
    CHECK_EQ(apply(d, kNeutral), 0);
}

TEST(empty_block_keeps_state) {
    hd_rumble::Decoder d;
    const uint8_t on = apply(d, kSteamStrong);
    CHECK_EQ(apply(d, Block{{0, 0, 0, 0}}), on);  // type 0: no samples
    apply(d, kNeutral);
    CHECK_EQ(apply(d, Block{{0, 0, 0, 0}}), 0);
}

TEST(five_bit_commands) {
    hd_rumble::Decoder d;
    // One 5-bit sample (type 1, low 20 bits zero): low band code 1 = substitute 2^0 (max).
    CHECK_EQ(apply(d, Block{{0x00, 0x00, 0x00, 0x42}}), 255);  // lo=1, hi=0
    // Code 3 = substitute 2^-1.
    const uint8_t half = apply(d, Block{{0x00, 0x00, 0x00, 0x46}});  // lo=3, hi=0
    CHECK(half > 200 && half < 255);
    // Code 31 = step down by 1/8 octave: quieter, not silent.
    const uint8_t down = apply(d, Block{{0x00, 0x00, 0x00, 0x7E}});  // lo=31, hi=0
    CHECK(down < half && down > 180);
    // Code 0 resets to silence.
    CHECK_EQ(apply(d, Block{{0x00, 0x00, 0x00, 0x40}}), 0);
}

TEST_MAIN()
