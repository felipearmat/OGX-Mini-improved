// Bluepad32 output queue with the byte-stream ring buffer patch (bluepad32_output_ring_buffer.diff,
// backport of ricardoquesada/bluepad32 b6531db). Regressions covered:
//  - the old 32-slot queue dropped packets when full, so the rumble "stop" queued after a burst of
//    rumble updates on a congested link was lost and the Switch pad kept vibrating;
//  - random put / get sequences match a plain FIFO (order, channel, length, bytes, wrap-around).
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include "test.h"
#include "uni_circular_buffer.h"

namespace {

constexpr int kRumbleLen = 11;  // Switch rumble-only report (0x10)

std::vector<uint8_t> rumble_packet(uint8_t level) {
    std::vector<uint8_t> p(kRumbleLen, 0);
    p[0] = 0xa2;
    p[1] = 0x10;
    for (int side = 3; side <= 7; side += 4) {
        if (level == 0) {
            p[side] = 0x00; p[side + 1] = 0x01; p[side + 2] = 0x40; p[side + 3] = 0x40;  // neutral
        } else {
            p[side] = 0x74; p[side + 1] = level; p[side + 2] = 0x3d; p[side + 3] = 0x72;
        }
    }
    return p;
}

struct Packet {
    int16_t cid;
    std::vector<uint8_t> bytes;
};

uint32_t rng_state = 1;
uint32_t rnd() {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

}  // namespace

TEST(stop_survives_a_burst_of_rumble_updates) {
    static uni_circular_buffer_t b;
    uni_circular_buffer_reset(&b);
    // 100 rumble updates queued while the link cannot send, then the stop.
    for (int i = 0; i < 100; i++) {
        const auto p = rumble_packet(static_cast<uint8_t>(0x48 + (i % 0x80)));
        CHECK_EQ(uni_circular_buffer_put(&b, 0x41, p.data(), kRumbleLen), UNI_CIRCULAR_BUFFER_ERROR_OK);
    }
    const auto stop = rumble_packet(0);
    CHECK_EQ(uni_circular_buffer_put(&b, 0x41, stop.data(), kRumbleLen), UNI_CIRCULAR_BUFFER_ERROR_OK);

    int delivered = 0;
    uint8_t last[128]{};
    int16_t cid = 0;
    int len = 0;
    while (uni_circular_buffer_get(&b, &cid, last, &len) == UNI_CIRCULAR_BUFFER_ERROR_OK)
        delivered++;
    CHECK_EQ(delivered, 101);
    CHECK_EQ(len, kRumbleLen);
    CHECK(std::memcmp(last, stop.data(), kRumbleLen) == 0);  // the stop goes out last
}

TEST(holds_over_250_rumble_packets_then_reports_full) {
    static uni_circular_buffer_t b;
    uni_circular_buffer_reset(&b);
    const auto p = rumble_packet(0x80);
    int queued = 0;
    while (uni_circular_buffer_put(&b, 0x41, p.data(), kRumbleLen) == UNI_CIRCULAR_BUFFER_ERROR_OK)
        queued++;
    CHECK(queued > 250);
    CHECK_EQ(uni_circular_buffer_put(&b, 0x41, p.data(), kRumbleLen), UNI_CIRCULAR_BUFFER_ERROR_BUFFER_FULL);
}

TEST(random_put_get_matches_a_fifo) {
    for (uint32_t seed = 1; seed <= 8; seed++) {
        static uni_circular_buffer_t b;
        uni_circular_buffer_reset(&b);
        rng_state = seed * 2654435761u;
        std::deque<Packet> model;
        int model_bytes = 0;
        const uint32_t put_bias = 35 + (seed * 9) % 50;
        const int max_len = (seed % 2) ? kRumbleLen : 128;
        for (int i = 0; i < 50000; i++) {
            if (rnd() % 100 < put_bias) {
                Packet p{static_cast<int16_t>(rnd() % 30000 + 1), std::vector<uint8_t>(rnd() % (max_len + 1))};
                for (auto& c : p.bytes) c = static_cast<uint8_t>(rnd());
                const int need = UNI_CIRCULAR_BUFFER_HEADER_SIZE + static_cast<int>(p.bytes.size());
                const bool fits = need <= (UNI_CIRCULAR_BUFFER_SIZE - 1) - model_bytes;
                const uint8_t rc = uni_circular_buffer_put(&b, p.cid, p.bytes.empty() ? nullptr : p.bytes.data(),
                                                           static_cast<int>(p.bytes.size()));
                CHECK_EQ(rc, fits ? UNI_CIRCULAR_BUFFER_ERROR_OK : UNI_CIRCULAR_BUFFER_ERROR_BUFFER_FULL);
                if (fits) {
                    model.push_back(p);
                    model_bytes += need;
                }
            } else {
                uint8_t out[144];
                std::memset(out, 0xa5, sizeof(out));
                int16_t cid = -1;
                int len = -1;
                const uint8_t rc = uni_circular_buffer_get(&b, &cid, out, &len);
                if (model.empty()) {
                    CHECK_EQ(rc, UNI_CIRCULAR_BUFFER_ERROR_BUFFER_EMPTY);
                    continue;
                }
                const Packet& e = model.front();
                CHECK_EQ(rc, UNI_CIRCULAR_BUFFER_ERROR_OK);
                CHECK_EQ(cid, e.cid);
                CHECK_EQ(len, static_cast<int>(e.bytes.size()));
                CHECK(std::memcmp(out, e.bytes.data(), e.bytes.size()) == 0);
                CHECK_EQ(out[e.bytes.size()], 0xa5);  // nothing written past the packet
                model_bytes -= UNI_CIRCULAR_BUFFER_HEADER_SIZE + len;
                model.pop_front();
            }
            if (test::failures() > 0)
                return;  // one report is enough
        }
    }
}

TEST_MAIN()
