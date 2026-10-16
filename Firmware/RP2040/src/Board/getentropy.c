#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pico/rand.h"

/* Custom: entropy for newlib's getentropy() / arc4random() and libstdc++'s std::random_device,
 * from the chip's own random number source (pico_rand). Without it the toolchain links a stub
 * that always fails (and warns: "_getentropy is not implemented and will always fail"). */
int _getentropy(void* buffer, size_t length)
{
    uint8_t* out = (uint8_t*)buffer;
    while (length > 0)
    {
        const uint32_t value = get_rand_32();
        const size_t n = length < sizeof(value) ? length : sizeof(value);
        memcpy(out, &value, n);
        out += n;
        length -= n;
    }
    return 0;
}
