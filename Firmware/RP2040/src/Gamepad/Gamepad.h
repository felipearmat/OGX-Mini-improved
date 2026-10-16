#ifndef _GAMEPAD_H_
#define _GAMEPAD_H_

#include <cstdint>
#include <atomic>
#include <limits>
#include <cstring>
#include <array>
#include <cmath>
#include <pico/mutex.h>
#include <pico/time.h>

#include "libfixmath/fix16.hpp"

#include "Gamepad/GamepadMapper.h"
#include "Gamepad/Lightbar.h"
#include "Gamepad/MotionSource.h"
#include "Gamepad/Range.h"
#include "Gamepad/fix16ext.h"
#include "UserSettings/UserProfile.h"
#include "UserSettings/JoystickSettings.h"
#include "UserSettings/TriggerSettings.h"
#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"
#include "Board/ogxm_log.h"

class Gamepad : public GamepadMapper
{
public:
    
    //Defaults used by device to get buttons: GamepadBits; host-side mapping: GamepadMapper

#pragma pack(push, 1)
    struct PadIn
    {
        /** IMU for PS3 Sixaxis / PS4 gadget: Sony, Switch Pro, Wii Remote, wired or BT. */
        static constexpr uint8_t MOTION_SRC_NONE = motion_source::NONE;  // Gamepad/MotionSource.h
        static constexpr uint8_t MOTION_SRC_DS4 = motion_source::DS4;
        static constexpr uint8_t MOTION_SRC_DS5 = motion_source::DS5;
        static constexpr uint8_t MOTION_SRC_SWITCH_PRO = motion_source::SWITCH_PRO;
        static constexpr uint8_t MOTION_SRC_DS5_USB = motion_source::DS5_USB;
        static constexpr uint8_t MOTION_SRC_DS4_USB = motion_source::DS4_USB;
        static constexpr uint8_t MOTION_SRC_SWITCH_USB = motion_source::SWITCH_USB;
        static constexpr uint8_t MOTION_SRC_WII_BT = motion_source::WII_BT;

        uint8_t  dpad;
        uint16_t buttons;
        uint8_t  trigger_l;
        uint8_t  trigger_r;
        int16_t  joystick_lx;
        int16_t  joystick_ly;
        int16_t  joystick_rx;
        int16_t  joystick_ry;
        uint8_t  analog[10];

        /** Bluepad / USB DS4+DS5: int32 accel (DS4 units/g) and gyro (DS4 rate units). */
        int32_t accel[3];
        int32_t gyro[3];
        /** If MOTION_SRC_NONE, PS3 mode leaves Sixaxis at neutral; else maps to DS3 report. */
        uint8_t motion_source;

        bool has_motion() const
        {
            return motion_source == MOTION_SRC_DS4 ||
                   motion_source == MOTION_SRC_DS5 ||
                   motion_source == MOTION_SRC_SWITCH_PRO ||
                   motion_source == MOTION_SRC_DS5_USB ||
                   motion_source == MOTION_SRC_DS4_USB ||
                   motion_source == MOTION_SRC_SWITCH_USB ||
                   motion_source == MOTION_SRC_WII_BT;
        }

        /** DS4 / DualSense touchpad raw bytes: 2 touch points x 4 bytes, the same wire format on
         *  both pads (bit 7 of a point's first byte set = not touching). */
        uint8_t touch_raw[8];
        uint8_t touchpad_click;
        /** Set when touch_raw was filled from a DS4 / DS5 report this frame. */
        uint8_t touchpad_valid;
        /** Custom: pad battery, Bluepad32 scale (0 = unknown, 1..255). */
        uint8_t battery;

        PadIn()
        {
            std::memset(this, 0, sizeof(PadIn));
        }
    };

    struct PadOut
    {
        uint8_t rumble_l;
        uint8_t rumble_r;

        PadOut()
        {
            std::memset(this, 0, sizeof(PadOut));
        }
    };

    using ChatpadIn = std::array<uint8_t, 3>;

#pragma pack(pop)

    Gamepad()
    {
        mutex_init(&pad_in_mutex_);
        mutex_init(&pad_out_mutex_);
        mutex_init(&chatpad_in_mutex_);
        reset_pad_in();
        reset_pad_out();
        reset_chatpad_in();
    };

    ~Gamepad() = default;

    //Get
    inline bool new_pad_in() const
	{
		return new_pad_in_.load(std::memory_order_acquire) ||
			   (bt_pad_middle_state_.load(std::memory_order_acquire) &
				BT_PAD_MAILBOX_DIRTY);
	}
    inline bool new_pad_out() const { return new_pad_out_.load(); }

    // True if current pad_out has non-zero rumble (read-only, does not clear new_pad_out).
    inline bool has_rumble()
    {
        mutex_enter_blocking(&pad_out_mutex_);
        bool r = (pad_out_.rumble_l > 0 || pad_out_.rumble_r > 0);
        mutex_exit(&pad_out_mutex_);
        return r;
    }

    //True if both host and device have enabled analog
    inline bool analog_enabled() const { return analog_enabled_.load(std::memory_order_relaxed); }

    inline PadIn get_pad_in()
    {
        mutex_enter_blocking(&pad_in_mutex_);
        /* Bluetooth HID runs on Core1; never block there on this mutex.
		 *
		 * Triple-buffer handoff:
		 *   - Core1 exclusively owns bt_pad_write_index_
		 *   - Core0 exclusively owns bt_pad_read_index_
		 *   - bt_pad_middle_state_ atomically contains the shared middle
		 *     buffer index plus a dirty bit
		 *
		 * If multiple Bluetooth reports arrive before Core0 consumes them,
		 * the newest complete PadIn wins.
		 */
		const unsigned middle_state =
			bt_pad_middle_state_.load(std::memory_order_acquire);

		if (middle_state & BT_PAD_MAILBOX_DIRTY) {
			const unsigned published =
				bt_pad_middle_state_.exchange(bt_pad_read_index_,
											  std::memory_order_acq_rel);

			if (published & BT_PAD_MAILBOX_DIRTY) {
				bt_pad_read_index_ = published & BT_PAD_MAILBOX_INDEX_MASK;

				const PadIn& p = bt_pending_pads_[bt_pad_read_index_];

				if (pad_in_count_ < PAD_IN_QUEUE_SIZE) {
					pad_in_queue_[pad_in_tail_] = p;
					pad_in_tail_ = (pad_in_tail_ + 1) % PAD_IN_QUEUE_SIZE;
					pad_in_count_++;
				} else {
					// Queue full: discard oldest so the newest controller state survives.
					pad_in_head_ = (pad_in_head_ + 1) % PAD_IN_QUEUE_SIZE;
					pad_in_queue_[pad_in_tail_] = p;
					pad_in_tail_ = (pad_in_tail_ + 1) % PAD_IN_QUEUE_SIZE;
				}

				new_pad_in_.store(true, std::memory_order_release);
			}
		}
        PadIn pad_in;
        if (pad_in_count_ > 0) {
            pad_in = pad_in_queue_[pad_in_head_];
            pad_in_head_ = (pad_in_head_ + 1) % PAD_IN_QUEUE_SIZE;
            pad_in_count_--;
            last_pad_in_ = pad_in;
            if (pad_in_count_ == 0) {
                new_pad_in_.store(false);
                /* Custom: diagnostics — time from the newest input's arrival to its use. */
                const uint32_t lat = static_cast<uint32_t>(time_us_64()) -
                                     pad_in_arrival_us_.load(std::memory_order_relaxed);
                ++latency_count_;
                latency_sum_us_ += lat;
                if (lat > latency_max_us_)
                    latency_max_us_ = lat;
            }
        } else {
            pad_in = last_pad_in_;
            /* Custom fix: nothing queued. What reset_pad_in() asked for (the neutral state, once)
             * is delivered now; the flag stayed set until a pad reported again, so with no pad
             * (after boot or a disconnect) every driver rebuilt and resent the same report on
             * every loop. A Bluetooth report published meanwhile keeps new_pad_in() true. */
            if (!(bt_pad_middle_state_.load(std::memory_order_acquire) & BT_PAD_MAILBOX_DIRTY))
                new_pad_in_.store(false);
        }
        mutex_exit(&pad_in_mutex_);
        return pad_in;
    }

    inline PadOut get_pad_out()
    {
        mutex_enter_blocking(&pad_out_mutex_);
        PadOut pad_out = pad_out_;
        new_pad_out_.store(false);
        mutex_exit(&pad_out_mutex_);

        return pad_out;
    }

    /* Custom: like get_pad_out(), but each motor reports the strongest rumble set since the
     * previous call. For consumers that poll slower than hosts send (Bluepad32 feedback runs
     * every 250 ms; Steam's trigger test pulses last ~200 ms), so a short pulse isn't lost. */
    inline PadOut take_pad_out_peak()
    {
        mutex_enter_blocking(&pad_out_mutex_);
        PadOut pad_out = pad_out_;
        if (peak_out_.rumble_l > pad_out.rumble_l) pad_out.rumble_l = peak_out_.rumble_l;
        if (peak_out_.rumble_r > pad_out.rumble_r) pad_out.rumble_r = peak_out_.rumble_r;
        peak_out_ = pad_out_;
        new_pad_out_.store(false);
        mutex_exit(&pad_out_mutex_);

        return pad_out;
    }

    /* Custom: lightbar colour requested by the host (PS4 mode), kept apart from PadOut so the
     * PadOut wire layout (I2C boards) doesn't change. */
    using Lightbar = ::Lightbar;  // Gamepad/Lightbar.h, shared with the ESP32

    inline void set_host_lightbar(uint8_t r, uint8_t g, uint8_t b)
    {
        mutex_enter_blocking(&pad_out_mutex_);
        host_lightbar_ = Lightbar{r, g, b, true};
        mutex_exit(&pad_out_mutex_);
    }

    inline Lightbar get_host_lightbar()
    {
        mutex_enter_blocking(&pad_out_mutex_);
        Lightbar l = host_lightbar_;
        mutex_exit(&pad_out_mutex_);
        return l;
    }

    inline ChatpadIn get_chatpad_in()
    {
        mutex_enter_blocking(&chatpad_in_mutex_);
        ChatpadIn chatpad_in = chatpad_in_;
        mutex_exit(&chatpad_in_mutex_);

        return chatpad_in;
    }

    //Set

    void set_analog_device(bool value) 
    { 
        analog_device_.store(value); 
        if (analog_host_.load() && analog_device_.load() && profile_analog_enabled_)
        {
            analog_enabled_.store(true);
        }
    }

    void set_analog_host(bool value) 
    { 
        analog_host_.store(value); 
        if (analog_host_.load() && analog_device_.load() && profile_analog_enabled_)
        {
            analog_enabled_.store(true);
        }
    }

    inline void set_pad_in(PadIn pad_in)
    {
        mutex_enter_blocking(&pad_in_mutex_);
        if (pad_in_count_ < PAD_IN_QUEUE_SIZE) {
            pad_in_queue_[pad_in_tail_] = pad_in;
            pad_in_tail_ = (pad_in_tail_ + 1) % PAD_IN_QUEUE_SIZE;
            pad_in_count_++;
        } else {
            /* Queue full: drop oldest, keep newest so fast taps (press+release) both get sent */
            pad_in_head_ = (pad_in_head_ + 1) % PAD_IN_QUEUE_SIZE;
            pad_in_queue_[pad_in_tail_] = pad_in;
            pad_in_tail_ = (pad_in_tail_ + 1) % PAD_IN_QUEUE_SIZE;
        }
        last_pad_in_ = pad_in;
        pad_in_arrival_us_.store(static_cast<uint32_t>(time_us_64()), std::memory_order_relaxed);
        new_pad_in_.store(true);
        mutex_exit(&pad_in_mutex_);
    }

    /**
     * High-rate hosts (Switch 0x30 ~1 kHz): keep only the newest PadIn.
     * Does not block on device output; intermediate analog samples may be dropped.
     */
    inline void set_pad_in_latest(PadIn pad_in)
    {
        mutex_enter_blocking(&pad_in_mutex_);
        pad_in_queue_[0] = pad_in;
        pad_in_head_ = 0;
        pad_in_tail_ = 1;
        pad_in_count_ = 1;
        last_pad_in_ = pad_in;
        pad_in_arrival_us_.store(static_cast<uint32_t>(time_us_64()), std::memory_order_relaxed);
        new_pad_in_.store(true);
        mutex_exit(&pad_in_mutex_);
    }

    /** Non-consuming copy of the most recently written PadIn (queue newest or last_). */
    inline PadIn peek_latest_pad_in()
    {
        mutex_enter_blocking(&pad_in_mutex_);
        PadIn pad_in;
        if (pad_in_count_ > 0) {
            const uint8_t idx =
                static_cast<uint8_t>((pad_in_tail_ + PAD_IN_QUEUE_SIZE - 1) % PAD_IN_QUEUE_SIZE);
            pad_in = pad_in_queue_[idx];
        } else {
            pad_in = last_pad_in_;
        }
        mutex_exit(&pad_in_mutex_);
        return pad_in;
    }

    /**
     * Replace the most recently written PadIn in place (or enqueue if empty).
     * Does not consume; used for same-frame host-side corrections (e.g. Cyclone face remap).
     */
    inline void overwrite_latest_pad_in(PadIn pad_in)
    {
        mutex_enter_blocking(&pad_in_mutex_);
        if (pad_in_count_ > 0) {
            const uint8_t idx =
                static_cast<uint8_t>((pad_in_tail_ + PAD_IN_QUEUE_SIZE - 1) % PAD_IN_QUEUE_SIZE);
            pad_in_queue_[idx] = pad_in;
        } else if (pad_in_count_ < PAD_IN_QUEUE_SIZE) {
            pad_in_queue_[pad_in_tail_] = pad_in;
            pad_in_tail_ = (pad_in_tail_ + 1) % PAD_IN_QUEUE_SIZE;
            pad_in_count_++;
        }
        last_pad_in_ = pad_in;
        pad_in_arrival_us_.store(static_cast<uint32_t>(time_us_64()), std::memory_order_relaxed);
        new_pad_in_.store(true);
        mutex_exit(&pad_in_mutex_);
    }

    /** Bluetooth (Core1): never blocks on Core0 — avoids stalling the BT stack in HID callbacks. */
	inline void set_pad_in_from_bluetooth(const PadIn& pad_in)
	{
		// Core1 exclusively owns bt_pad_write_index_.
		bt_pending_pads_[bt_pad_write_index_] = pad_in;

		// Publish this completed buffer together with the dirty state,
		// and take ownership of the previous middle buffer.
		const unsigned previous_middle =
			bt_pad_middle_state_.exchange(
				bt_pad_write_index_ | BT_PAD_MAILBOX_DIRTY,
				std::memory_order_acq_rel);

		bt_pad_write_index_ =
			previous_middle & BT_PAD_MAILBOX_INDEX_MASK;

		pad_in_arrival_us_.store(static_cast<uint32_t>(time_us_64()), std::memory_order_relaxed);
		new_pad_in_.store(true, std::memory_order_release);
	}

    inline void set_pad_out(const PadOut& pad_out)
    {
        mutex_enter_blocking(&pad_out_mutex_);
        pad_out_ = pad_out;
        if (pad_out.rumble_l > peak_out_.rumble_l) peak_out_.rumble_l = pad_out.rumble_l;
        if (pad_out.rumble_r > peak_out_.rumble_r) peak_out_.rumble_r = pad_out.rumble_r;
        new_pad_out_.store(true);
        mutex_exit(&pad_out_mutex_);
    }

    inline void set_chatpad_in(const ChatpadIn& chatpad_in)
    {
        mutex_enter_blocking(&chatpad_in_mutex_);
        chatpad_in_ = chatpad_in;
        mutex_exit(&chatpad_in_mutex_);
    }

    /* Custom: diagnostics. How long the newest controller input waited before the output driver
     * (or anything else) read it: samples, average and largest wait in microseconds. */
    struct LatencyStats {
        uint32_t samples;
        uint32_t avg_us;
        uint32_t max_us;
    };
    LatencyStats latency_stats()
    {
        mutex_enter_blocking(&pad_in_mutex_);
        const LatencyStats st{latency_count_,
                              latency_count_ ? static_cast<uint32_t>(latency_sum_us_ / latency_count_) : 0,
                              latency_max_us_};
        mutex_exit(&pad_in_mutex_);
        return st;
    }

    // Wii U GC adapter: set by host when controller uses positive Y for physical up (e.g. Xbox One/360)
    void set_stick_y_positive_is_up(bool v) { stick_y_positive_is_up_ = v; }
    bool stick_y_positive_is_up() const { return stick_y_positive_is_up_; }

    /* Custom: set by Bluetooth for a lone Joy-Con (no D-pad); mode combos then read the left
     * stick held at the edge as the D-pad (UserSettings::check_for_driver_change). */
    void set_combo_stick_as_dpad(bool v) { combo_stick_as_dpad_.store(v); }
    bool combo_stick_as_dpad() const { return combo_stick_as_dpad_.load(); }

    inline void reset_pad_in()
    {
		bt_pad_middle_state_.fetch_and(BT_PAD_MAILBOX_INDEX_MASK, std::memory_order_acq_rel);
        mutex_enter_blocking(&pad_in_mutex_);
        pad_in_head_ = 0;
        pad_in_tail_ = 0;
        pad_in_count_ = 0;
        last_pad_in_ = PadIn();
        mutex_exit(&pad_in_mutex_);
        new_pad_in_.store(true);
    }
    
    inline void reset_pad_out()
    {
        mutex_enter_blocking(&pad_out_mutex_);
        pad_out_ = PadOut();
        peak_out_ = PadOut();
        host_lightbar_ = Lightbar();
        new_pad_out_.store(true);
        mutex_exit(&pad_out_mutex_);
    }

    inline void reset_chatpad_in()
    {
        mutex_enter_blocking(&chatpad_in_mutex_);
        chatpad_in_.fill(0);
        mutex_exit(&chatpad_in_mutex_);
    }

    static constexpr unsigned PAD_IN_QUEUE_SIZE = 8;
	static constexpr unsigned BT_PAD_MAILBOX_SLOTS = 3;
	static constexpr unsigned BT_PAD_MAILBOX_DIRTY = 1u << 2;
	static constexpr unsigned BT_PAD_MAILBOX_INDEX_MASK = BT_PAD_MAILBOX_DIRTY - 1u;

private:
    PadIn bt_pending_pads_[BT_PAD_MAILBOX_SLOTS]{};

	// Triple-buffer ownership starts as:
	//   Core1 write buffer = 0
	//   shared middle     = 1
	//   Core0 read buffer = 2
	unsigned bt_pad_write_index_{0};                 // Core1 only
	std::atomic<unsigned> bt_pad_middle_state_{1};   // middle index + dirty bit
	unsigned bt_pad_read_index_{2};                  // Core0 only
    mutex_t pad_in_mutex_;
    mutex_t pad_out_mutex_;
    mutex_t chatpad_in_mutex_;

    PadOut pad_out_;
    PadOut peak_out_;  // strongest rumble since the last take_pad_out_peak()
    Lightbar host_lightbar_;
    PadIn pad_in_queue_[PAD_IN_QUEUE_SIZE];
    unsigned pad_in_head_{0};
    unsigned pad_in_tail_{0};
    unsigned pad_in_count_{0};
    PadIn last_pad_in_{};
    ChatpadIn chatpad_in_{0};

    std::atomic<bool> new_pad_in_{false};
    /* Custom: diagnostics, input-to-use latency (see latency_stats()). */
    std::atomic<uint32_t> pad_in_arrival_us_{0};
    uint32_t latency_count_{0};
    uint64_t latency_sum_us_{0};
    uint32_t latency_max_us_{0};
    std::atomic<bool> new_pad_out_{false};

    std::atomic<bool> analog_enabled_{false};
    std::atomic<bool> analog_host_{false};
    std::atomic<bool> analog_device_{false};

    bool stick_y_positive_is_up_{false};  // true for Xbox (positive Y = up), false for Wii U/Nintendo
    std::atomic<bool> combo_stick_as_dpad_{false};

};

#endif // _GAMEPAD_H_