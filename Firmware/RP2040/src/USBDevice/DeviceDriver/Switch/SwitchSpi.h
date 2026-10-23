#ifndef _OGXM_CUSTOM_SWITCH_SPI_H_
#define _OGXM_CUSTOM_SWITCH_SPI_H_

#include <cstddef>
#include <cstdint>

/*  The emulated Switch Pro Controller's SPI flash (custom fix, not upstream), as hosts read it with
 *  subcommand 0x10. Reads used to be answered by exact start address only: Linux hid-nintendo reads
 *  the right stick's factory calibration on its own at 0x6046 and got 0xFF (centre 4095), so the
 *  right stick sat in one quadrant at a third of its travel. Now any address and length within the
 *  factory block (0x6000-0x60A9) gets its bytes; everything else (user calibration at 0x8010-0x803F
 *  included: no magic, so hosts use the factory data) reads 0xFF.
 */
namespace switch_spi {

    constexpr uint8_t kMaxRead = 0x1D;  // a subcommand reply holds 29 bytes of data

    /* The output's sticks (Switch.cpp): 12-bit, centre 2047, int16 / 22 (as the wired Pro host's
     * * 22), so a full stick is 2047 +- 1489. The factory calibration declares this centre and a
     * range a little short of 1489, so a pad that stops short of full still reaches the edge. It
     * used to be a real Pro Controller's (right centre 2141, ranges 1414-1566): once hosts read the
     * right stick's at 0x6046 it sat off centre and short of its travel. */
    constexpr int32_t kStickCenter = 2047;
    constexpr int32_t kStickGainInv = 22;
    constexpr int32_t kStickCalRange = 1420;
    static_assert(kStickCalRange <= 32767 / kStickGainInv, "a full stick must reach the declared range");

    // Copies `len` bytes (at most kMaxRead) from address `addr` into `out`.
    void read(uint32_t addr, uint8_t len, uint8_t* out);

} // namespace switch_spi

#endif // _OGXM_CUSTOM_SWITCH_SPI_H_
