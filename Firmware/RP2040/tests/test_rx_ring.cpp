// UART receive ring (Board/RxRing.h): the hybrid's ESP32 output, written by DMA, read back by
// the main loop in order, without the bytes the UART FIFO used to drop while the loop was busy.
#include <cstdint>
#include <string>

#include "Board/RxRing.h"
#include "test.h"

namespace {

using Ring = RxRing<4>;  // 16 bytes

// What the DMA does: each byte at the next position of the ring.
void write(Ring& ring, uint32_t& produced, const std::string& text)
{
    for (const char c : text)
        ring.buffer()[produced++ & (Ring::kSize - 1)] = static_cast<uint8_t>(c);
}

std::string drain(Ring& ring, uint32_t produced, uint32_t* lost = nullptr)
{
    std::string got;
    const uint32_t n = ring.drain(produced, [&](uint8_t b) { got.push_back(static_cast<char>(b)); });
    if (lost)
        *lost = n;
    return got;
}

} // namespace

TEST(bytes_come_back_in_order_across_the_wrap) {
    static Ring ring;
    uint32_t produced = 0;
    write(ring, produced, "0123456789");
    CHECK((drain(ring, produced)) == std::string("0123456789"));
    write(ring, produced, "abcdefghij");  // wraps past the end of the 16 bytes
    CHECK((drain(ring, produced)) == std::string("abcdefghij"));
    CHECK((drain(ring, produced)) == std::string());  // nothing new
}

TEST(a_whole_ring_written_while_the_loop_was_busy_is_kept) {
    static Ring ring;
    uint32_t produced = 0;
    write(ring, produced, "0123456789abcdef");  // exactly the ring size
    uint32_t lost = 99;
    CHECK((drain(ring, produced, &lost)) == std::string("0123456789abcdef"));
    CHECK_EQ(lost, 0u);
}

TEST(an_overrun_keeps_the_newest_bytes_and_counts_the_lost_ones) {
    static Ring ring;
    uint32_t produced = 0;
    write(ring, produced, "0123456789abcdefXYZ");  // 19 bytes: the first 3 overwritten
    uint32_t lost = 0;
    CHECK((drain(ring, produced, &lost)) == std::string("3456789abcdefXYZ"));
    CHECK_EQ(lost, 3u);
}

TEST(the_byte_count_wrapping_at_2_32_changes_nothing) {
    static Ring ring;
    uint32_t produced = 0;
    // Bring the reader and the writer just below 2^32.
    produced = 0xFFFFFFF8u;
    ring.drain(produced, [](uint8_t) {});
    write(ring, produced, "wrap-ok!!");
    uint32_t lost = 99;
    CHECK((drain(ring, produced, &lost)) == std::string("wrap-ok!!"));
    CHECK_EQ(lost, 0u);
}

TEST_MAIN()
