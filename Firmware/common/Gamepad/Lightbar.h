#ifndef _OGXM_LIGHTBAR_H_
#define _OGXM_LIGHTBAR_H_

#include <cstdint>

/* A DS4 / DualSense lightbar colour: asked by the host (PS4 mode) or the output mode's colour. */
struct Lightbar
{
    uint8_t r{0}, g{0}, b{0};
    bool valid{false};

    bool same_colour(const Lightbar& other) const
    {
        return r == other.r && g == other.g && b == other.b;
    }
};

#endif // _OGXM_LIGHTBAR_H_
