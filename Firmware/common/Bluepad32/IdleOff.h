#ifndef _OGXM_IDLE_OFF_H_
#define _OGXM_IDLE_OFF_H_

#include <cstdint>

#include "uni.h"
#include "Bluepad32/IdleMotion.h"

/* A Bluetooth controller left on without input is turned off after the dongle option's idle time
 * (UserSettings/DongleSettings idle_off_minutes; 0 = never), to save its battery. Tracked per
 * output gamepad, so a merged Joy-Con pair counts as one: any button, D-pad, a stick / trigger
 * moving past kAxisDelta, or the motion sensors showing the controller being handled
 * (Bluepad32/IdleMotion.h: offsets, noise, spikes and its own rumble do not count) is input. The
 * platform turns the pads of an output off with bt_pad::turn_off(). */
class IdleOff
{
public:
    static constexpr int32_t kAxisDelta = 48;  // Bluepad32 units: sticks -512..511, triggers 0..1023

    /* The output starts its idle time again with its next controller. */
    void reset()
    {
        valid_ = false;
        turned_off_ = false;
        rumble_seen_ = false;
        motion_.reset();
    }

    /* The host asked this output for rumble on this feedback tick. */
    void note_rumble(uint32_t now_ms)
    {
        rumble_seen_ = true;
        rumble_ms_ = now_ms;
    }

    /* Every report of a pad feeding this output. rumble_window_ms: how long after a rumble
     * request the pad may still shake (the feedback period plus a margin). */
    void note_input(const uni_gamepad_t* gp, uint32_t now_ms, uint32_t rumble_window_ms)
    {
        const int32_t axis[6] = {gp->axis_x, gp->axis_y, gp->axis_rx, gp->axis_ry, gp->brake, gp->throttle};
        const bool rumbling = rumble_seen_ && now_ms - rumble_ms_ < rumble_window_ms;
        const bool handled = motion_.update(gp->gyro, gp->accel, now_ms, rumbling);  // every report
        bool moved = !valid_ || handled || gp->buttons != buttons_ || gp->dpad != dpad_ || gp->misc_buttons != misc_;
        for (int i = 0; i < 6 && !moved; ++i) {
            const int32_t delta = axis[i] - axis_[i];
            moved = delta > kAxisDelta || delta < -kAxisDelta;
        }
        if (!moved)
            return;
        valid_ = true;
        turned_off_ = false;
        last_input_ms_ = now_ms;
        buttons_ = gp->buttons;
        dpad_ = gp->dpad;
        misc_ = gp->misc_buttons;
        for (int i = 0; i < 6; ++i)
            axis_[i] = axis[i];
    }

    /* True once when the output has had no input for `minutes` (0 = never): turn its pads off. */
    bool due(uint32_t now_ms, uint8_t minutes)
    {
        if (minutes == 0 || !valid_ || turned_off_ ||
            now_ms - last_input_ms_ < static_cast<uint32_t>(minutes) * 60000u)
            return false;
        turned_off_ = true;
        return true;
    }

private:
    bool valid_{false};
    bool turned_off_{false};
    bool rumble_seen_{false};
    uint32_t last_input_ms_{0};
    uint32_t rumble_ms_{0};  // last feedback tick that asked for rumble
    idle_motion::Detector motion_;
    uint16_t buttons_{0};
    uint8_t dpad_{0};
    uint8_t misc_{0};
    int32_t axis_[6]{};
};

#endif // _OGXM_IDLE_OFF_H_
