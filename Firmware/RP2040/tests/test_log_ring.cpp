// Kept log (Board/LogRing.h): GET_LOG reads from a position, so a reader polling now and then
// gets every new byte once, and learns how many were dropped when it fell behind.
#include <cstdint>
#include <cstring>
#include <string>

#include "Board/LogRing.h"
#include "test.h"

namespace {

std::string read_since(uint32_t since, uint32_t& start, uint32_t& total, size_t len = 4096)
{
    static uint8_t out[64 * 1024];
    const size_t n = log_ring_read_since(since, &start, &total, out, len < sizeof(out) ? len : sizeof(out));
    return std::string(reinterpret_cast<char*>(out), n);
}

void append(const char* source, const std::string& text) { log_ring_append(source, text.data(), text.size()); }

} // namespace

TEST(lines_get_their_source_and_a_reader_gets_each_byte_once) {
    uint32_t start = 0, total = 0;
    const std::string before = read_since(0, start, total);
    const uint32_t mark = total;
    append("esp", "I (123) boot\n");
    append("rp", "1.500 slot 0 connected\n");
    std::string got = read_since(mark, start, total);
    CHECK_EQ(start, mark);
    CHECK((got) == std::string("[esp] I (123) boot\n[rp] 1.500 slot 0 connected\n"));
    CHECK_EQ(total, mark + static_cast<uint32_t>(got.size()));
    CHECK((read_since(total, start, total)) == std::string());  // nothing new
}

TEST(a_line_split_across_appends_keeps_one_prefix) {
    uint32_t start = 0, total = 0;
    read_since(0, start, total);
    const uint32_t mark = total;
    append("esp", "W (5) half");
    append("esp", " and the rest\n");
    CHECK((read_since(mark, start, total)) == std::string("[esp] W (5) half and the rest\n"));
}

TEST(another_source_does_not_join_an_open_line) {
    uint32_t start = 0, total = 0;
    read_since(0, start, total);
    const uint32_t mark = total;
    append("esp", "BTstack up and running at ");
    append("rp", "1.282 USB configured by the host\n");
    append("esp", "38:31:52:AA:BB:CC\n");
    CHECK((read_since(mark, start, total)) == std::string("[esp] BTstack up and running at \n"
                                                          "[rp] 1.282 USB configured by the host\n"
                                                          "[esp] 38:31:52:AA:BB:CC\n"));
}

TEST(a_short_read_continues_where_it_stopped) {
    uint32_t start = 0, total = 0;
    read_since(0, start, total);
    const uint32_t mark = total;
    append("rp", "abcdefghij\n");
    const std::string first = read_since(mark, start, total, 6);
    const std::string rest = read_since(mark + 6, start, total);
    CHECK((first + rest) == std::string("[rp] abcdefghij\n"));
    CHECK_EQ(start, mark + 6);
}

TEST(a_reader_that_fell_behind_starts_at_the_oldest_kept_byte) {
    uint32_t start = 0, total = 0;
    read_since(0, start, total);
    const uint32_t mark = total;
    const std::string line(99, 'x');
    for (int i = 0; i < 2000; ++i)  // 200 KB, more than any ring size
        append("esp", line + "\n");
    const std::string got = read_since(mark, start, total, 64 * 1024);
    CHECK(start > mark);  // bytes were dropped: the reader can say how many
    CHECK_EQ(total - start, static_cast<uint32_t>(log_ring_size()));
    CHECK(got.size() == log_ring_size() || got.size() == 64 * 1024);
}

TEST(a_position_ahead_of_the_ring_reads_from_the_oldest) {
    // The adapter restarted with a fresh ring (power cycle): the reader's position is ahead.
    uint32_t start = 0, total = 0;
    read_since(0, start, total);
    read_since(total + 100000, start, total);
    CHECK_EQ(total - start, static_cast<uint32_t>(log_ring_size()));
}

TEST_MAIN()
