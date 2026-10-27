#ifndef _OGXM_HD_RUMBLE_FEED_H_
#define _OGXM_HD_RUMBLE_FEED_H_

#include <cstdint>
#include <cstring>

/* Switch HD Rumble passed through (OGX-Mini-improved, Firmware/RP2040/docs/HD_Rumble.md): in Switch
 * mode a Joy-Con or Switch Pro Controller plays the host's own motor blocks, as on a Switch; every
 * other pad and mode keeps the intensity rumble. Shared by the Pico W path and the hybrid's ESP32.
 *
 * Each host report carries 8 bytes: the left and right 4-byte blocks. A block may hold relative
 * commands (step up / down from the pad's current level), so every report must reach the pad once
 * and in order: the platform queues them (Frame) and hands them to Feed one by one. */
namespace hd_rumble {

    /* The BT core drains the queue at this pace; a Switch host sends a report about every 15 ms. */
    constexpr uint32_t kPeriodMs = 8;
    /* No report for this long while the pad plays: one neutral report stops it (a host that stopped
     * sending: USB suspend, an emulator closing). The pad holds the last blocks otherwise. */
    constexpr uint32_t kStopAfterMs = 100;
    /* HD Rumble owns the pad's rumble this long after the last report: the intensity path stays off
     * (no double rumble from the same host request). */
    constexpr uint32_t kOwnMs = 1000;

    constexpr uint8_t kNeutralBlock[4] = {0x00, 0x01, 0x40, 0x40};

    struct Frame
    {
        uint8_t blocks[8];
    };

    inline bool is_neutral(const uint8_t blocks[8])
    {
        return std::memcmp(&blocks[0], kNeutralBlock, 4) == 0 && std::memcmp(&blocks[4], kNeutralBlock, 4) == 0;
    }

    inline Frame neutral_frame()
    {
        Frame f;
        std::memcpy(&f.blocks[0], kNeutralBlock, 4);
        std::memcpy(&f.blocks[4], kNeutralBlock, 4);
        return f;
    }

    /* One output's state on the BT side. */
    class Feed
    {
    public:
        /* A host report taken from the queue at now_ms: send it as is. */
        void on_frame(const Frame& f, uint32_t now_ms)
        {
            last_ms_ = now_ms;
            seen_ = true;
            playing_ = !is_neutral(f.blocks);
        }

        /* Each tick with the queue empty: true when the pads need one neutral report now. */
        bool stop_due(uint32_t now_ms)
        {
            if (!playing_ || now_ms - last_ms_ < kStopAfterMs)
                return false;
            playing_ = false;
            return true;
        }

        /* HD Rumble drives this output's Switch pads (the intensity rumble stays off). */
        bool owns_rumble(uint32_t now_ms) const { return seen_ && now_ms - last_ms_ < kOwnMs; }

        bool playing() const { return playing_; }

        void reset() { *this = Feed(); }

    private:
        uint32_t last_ms_{0};
        bool seen_{false};
        bool playing_{false};
    };

    /* The RP2040 + ESP32 link (I2C/Esp32Bridge.h GET_HD_RUMBLE). RP2040 side: the report in flight
     * is answered again until the ESP32 acknowledges its seq, then the next one is taken. */
    class LinkSender
    {
    public:
        /* `pop(Frame&)` takes the next queued report; returns the report to answer with, or null. */
        template <typename Pop>
        const Frame* reply(uint8_t ack_seq, Pop pop)
        {
            if (pending_ && ack_seq == seq_)
                pending_ = false;
            if (!pending_ && pop(frame_)) {
                seq_ = static_cast<uint8_t>(seq_ == 255 ? 1 : seq_ + 1);
                pending_ = true;
            }
            return pending_ ? &frame_ : nullptr;
        }
        uint8_t seq() const { return seq_; }

    private:
        Frame frame_{};
        uint8_t seq_{0};
        bool pending_{false};
    };

    /* ESP32 side: a reply is a new report when its seq differs from the last one taken. */
    class LinkReceiver
    {
    public:
        bool take(uint8_t seq)
        {
            if (seq == 0 || seq == last_)
                return false;
            last_ = seq;
            return true;
        }
        uint8_t ack() const { return last_; }

    private:
        uint8_t last_{0};
    };

} // namespace hd_rumble

#endif // _OGXM_HD_RUMBLE_FEED_H_
