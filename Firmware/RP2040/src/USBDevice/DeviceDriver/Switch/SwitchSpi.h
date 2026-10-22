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

    // Copies `len` bytes (at most kMaxRead) from address `addr` into `out`.
    void read(uint32_t addr, uint8_t len, uint8_t* out);

} // namespace switch_spi

#endif // _OGXM_CUSTOM_SWITCH_SPI_H_
