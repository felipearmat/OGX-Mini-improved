#ifndef _OGXM_BT_PAD_H_
#define _OGXM_BT_PAD_H_

#include <cstdint>
#include <cstring>
#include <tuple>

#include "uni.h"
#include "parser/uni_hid_parser_ds3.h"
#include "parser/uni_hid_parser_ds4.h"
#include "parser/uni_hid_parser_ds5.h"
#include "parser/uni_hid_parser_psmove.h"
#include "parser/uni_hid_parser_stadia.h"
#include "parser/uni_hid_parser_steam_triton.h"
#include "parser/uni_hid_parser_switch.h"
#include "parser/uni_hid_parser_switch2.h"
#include "parser/uni_hid_parser_wii.h"
#include "parser/uni_hid_parser_xboxone.h"

#include "Bluepad32/ComboHold.h"
#include "Bluepad32/DropPolicy.h"
#include "Bluepad32/JoyConSettings.h"
#include "Gamepad/Lightbar.h"
#include "Gamepad/MotionSource.h"
#include "Gamepad/Ps5Core.h"
#include "Gamepad/GamepadMapper.h"
#include "Gamepad/RumbleTiming.h"

/* Bluepad32 controller handling shared by every board that runs Bluepad32: the Pico W / Pico 2 W
 * (Bluepad32 on the Pico's own radio) and the ESP32 companion of the RP2040 + ESP32 hybrid. The
 * platform keeps what differs (time source, reboot, where the input goes: a local Gamepad or the
 * I2C packet); everything that decides how a pad behaves lives here, so a fix reaches both. */
namespace bt_pad {

    /* Disconnect combo: Start + Select (L3 + R3 on an OUYA pad, which has neither) held for
     * kDisconnectComboHoldMs, not in the first kDisconnectComboGraceMs after the pad is ready.
     * The platform disconnects from a run-loop timer (doing it while Bluepad32 parses the pad's
     * own report hung the BT core), then reboots after kComboRebootDelayMs for a clean radio:
     * after the disconnect the adapter stopped accepting the pad's own reconnection. */
    constexpr uint32_t kDisconnectComboHoldMs = 3000;
    constexpr uint32_t kDisconnectComboGraceMs = 3500;
    constexpr uint32_t kComboRebootDelayMs = 1500;

    /* Host-to-pad output while idle (no rumble), or the BLE link drops: an Xbox pad puts it to
     * sleep after about a minute; a Switch 2 Pro drops it (HCI 0x08) without periodic vibration
     * writes, so it gets one every feedback period. */
    constexpr uint32_t kXboxBleKeepaliveMs = 12000;
    constexpr uint32_t kSwitch2BleKeepaliveMs = 8;

    inline bool is_switch_joycon(const uni_hid_device_t* d)
    {
        return d != nullptr && (d->controller_type == CONTROLLER_TYPE_SwitchJoyConLeft ||
                                d->controller_type == CONTROLLER_TYPE_SwitchJoyConRight);
    }

    /* A pad that plays a Switch host's HD Rumble blocks as they are (Gamepad/HdRumbleFeed.h): the
     * Switch parser's Joy-Cons and Pro Controllers (a Switch 2 pad has another rumble format). */
    inline bool plays_hd_rumble(const uni_hid_device_t* d)
    {
        return d != nullptr && d->report_parser.play_dual_rumble == uni_hid_parser_switch_play_dual_rumble &&
               (is_switch_joycon(d) || d->controller_type == CONTROLLER_TYPE_SwitchProController);
    }

    inline bool is_joycon_pair_secondary(const uni_hid_device_t* d)
    {
        if (uni_hid_parser_switch2_is_ble_device(d))
            return uni_hid_parser_switch2_is_joycon_pair_secondary(d);
        if (is_switch_joycon(d))
            return uni_hid_parser_switch_is_joycon_pair_secondary(d);
        return false;
    }

    /* The output pad a device feeds: both halves of a Joy-Con pair feed one. */
    inline int output_idx(uni_hid_device_t* d)
    {
        if (uni_hid_parser_switch2_is_ble_device(d))
            return uni_hid_parser_switch2_get_gamepad_output_idx(d);
        if (is_switch_joycon(d))
            return uni_hid_parser_switch_get_gamepad_output_idx(d);
        return uni_hid_device_get_idx_for_instance(d);
    }

    inline int pair_partner_idx(uni_hid_device_t* d)
    {
        if (uni_hid_parser_switch2_is_ble_device(d))
            return uni_hid_parser_switch2_get_pair_partner_idx(d);
        if (is_switch_joycon(d))
            return uni_hid_parser_switch_get_pair_partner_idx(d);
        return -1;
    }

    /* Turn one pad off (idle time over). gap_disconnect() does not complete on live Joy-Con
     * links: those are asked to sleep instead. */
    inline void turn_off(uni_hid_device_t* d)
    {
        if (is_switch_joycon(d))
            uni_hid_parser_switch_request_sleep(d);
        else
            uni_hid_device_disconnect(d);
    }

    /* Drop a pad and its Joy-Con partner (the disconnect combo). gap_disconnect() does not complete
     * on live Joy-Con links: those are asked to sleep instead. */
    inline void disconnect_with_partner(uni_hid_device_t* d)
    {
        if (d == nullptr)
            return;
        const int partner_idx = pair_partner_idx(d);
        uni_hid_device_t* partner = (partner_idx >= 0 && partner_idx < CONFIG_BLUEPAD32_MAX_DEVICES)
                                        ? uni_hid_device_get_instance_for_idx(partner_idx)
                                        : nullptr;
        if (partner == d)
            partner = nullptr;
        if (is_switch_joycon(d)) {
            if (partner)
                uni_hid_parser_switch_request_sleep(partner);
            uni_hid_parser_switch_request_sleep(d);
        } else {
            if (partner)
                uni_hid_device_disconnect(partner);
            uni_hid_device_disconnect(d);
        }
    }

    inline bool is_wii(const uni_hid_device_t* d)
    {
        return d != nullptr && d->controller_type == CONTROLLER_TYPE_WiiController;
    }

    /* TV-style Wiimote (not Wii U Pro, Classic, or Balance Board). */
    inline bool is_handheld_wiimote(const uni_hid_device_t* d)
    {
        if (!is_wii(d))
            return false;
        switch (d->controller_subtype) {
            case CONTROLLER_SUBTYPE_WIIUPRO:
            case CONTROLLER_SUBTYPE_WII_CLASSIC:
            case CONTROLLER_SUBTYPE_WII_BALANCE_BOARD:
                return false;
            default:
                return true;
        }
    }

    inline bool has_nunchuk(const uni_hid_device_t* d)
    {
        return is_wii(d) && (d->controller_subtype == CONTROLLER_SUBTYPE_WIIMOTE_NUNCHUK ||
                             d->controller_subtype == CONTROLLER_SUBTYPE_WIIMOTE_NUNCHUK_ACCEL);
    }

    /* DS4 / DualSense over Bluetooth also create a "virtual mouse" device on the same link: only
     * gamepad input is used, and accepting that slot destabilized the link (drop after ~2 s). */
    inline bool reject_on_ready(uni_hid_device_t* d)
    {
        return uni_hid_device_is_virtual_device(d);
    }

    /* Pad setup once it is ready: player LED, Wiimote held upright, DualSense adaptive triggers
     * off. motion_output: the output mode uses motion (Wiimote accelerometer reports). */
    inline void setup_on_ready(uni_hid_device_t* d, uint8_t pad_idx, bool motion_output)
    {
        /* Player LED of the output slot (e.g. Wii U: LED 1 = player 1). A Joy-Con pair sets its
         * LEDs when it merges: the secondary half is left alone. */
        if (d->report_parser.set_player_leds != nullptr && !is_joycon_pair_secondary(d))
            d->report_parser.set_player_leds(d, static_cast<uint8_t>(1u << pad_idx));

        /* Wiimote: Bluepad32 defaults to sideways (horizontal) mapping; OGX uses TV-style
         * vertical. set_mode() overwrites controller_subtype: the extension is saved first. */
        if (is_handheld_wiimote(d)) {
            const bool nunchuk = has_nunchuk(d);
            uni_hid_parser_wii_set_mode(d, WII_MODE_VERTICAL);

            /* Motion output: request accel reports. With a nunchuk, DRM_KA (0x31) has no
             * extension bytes: DRM_KAE (0x35) keeps stick + accel. Byte 0x04 = continuous
             * reporting (wiibrew); without it the Wiimote only reports large changes. The mode
             * stays VERTICAL (not WII_MODE_ACCEL) so the button layout is unchanged. */
            if (motion_output) {
                if (nunchuk) {
                    d->controller_subtype = CONTROLLER_SUBTYPE_WIIMOTE_NUNCHUK_ACCEL;
                    static constexpr uint8_t kWiiReqAccelNunchuk[] = {0xa2, 0x12, 0x04, 0x35};
                    uni_hid_device_send_intr_report(d, kWiiReqAccelNunchuk, sizeof(kWiiReqAccelNunchuk));
                } else {
                    d->controller_subtype = CONTROLLER_SUBTYPE_WIIMOTE_ACCEL;
                    static constexpr uint8_t kWiiReqAccel[] = {0xa2, 0x12, 0x04, 0x31};
                    uni_hid_device_send_intr_report(d, kWiiReqAccel, sizeof(kWiiReqAccel));
                }
            } else if (nunchuk) {
                d->controller_subtype = CONTROLLER_SUBTYPE_WIIMOTE_NUNCHUK;
            }
        }

        /* DualSense: start with adaptive triggers off (the touchpad press toggles them where
         * the platform supports it). */
        if (d->controller_type == CONTROLLER_TYPE_PS5Controller) {
            ds5_adaptive_trigger_effect_t off = ds5_new_adaptive_trigger_effect_off();
            ds5_set_adaptive_trigger_effect(d, UNI_ADAPTIVE_TRIGGER_TYPE_LEFT, &off);
            ds5_set_adaptive_trigger_effect(d, UNI_ADAPTIVE_TRIGGER_TYPE_RIGHT, &off);
        }
    }

    /* No host rumble on a freshly connected DualShock 4 before this time (ms from now): early
     * output reports can drop its link. 0 for other pads. */
    inline uint32_t host_rumble_grace_ms(const uni_hid_device_t* d)
    {
        return d->controller_type == CONTROLLER_TYPE_PS4Controller ? connect_rumble::kDs4HostRumbleGraceMs : 0;
    }

    /* A short rumble once the pad is ready tells the user it is connected (deferred on a DS4,
     * same reason as above). */
    inline void play_connection_rumble(uni_hid_device_t* d)
    {
        if (d == nullptr || d->report_parser.play_dual_rumble == nullptr)
            return;
        const uint16_t start_delay = (d->controller_type == CONTROLLER_TYPE_PS4Controller)
                                         ? connect_rumble::kDelayDs4Ms
                                         : connect_rumble::kDelayDefaultMs;
        d->report_parser.play_dual_rumble(d, start_delay, connect_rumble::kDurationMs,
                                          connect_rumble::kWeak, connect_rumble::kStrong);
    }

    /* Host rumble on the pad. The host's left motor (rumble_l) is the strong / low-frequency one
     * and the right motor (rumble_r) the weak one, while Bluepad32's *_play_dual_rumble() take
     * (weak, strong). length: how long it plays, longer than the feedback period that renews
     * it. Calling the parsers directly also avoids a function pointer crash in Bluepad32. */
    inline void play_rumble(uni_hid_device_t* d, uint16_t length, uint8_t rumble_l, uint8_t rumble_r)
    {
        const uint8_t weak = rumble_r;
        const uint8_t strong = rumble_l;
        switch (d->controller_type) {
            case CONTROLLER_TYPE_XBoxOneController:
                uni_hid_parser_xboxone_play_dual_rumble(d, 0, length + 10, weak, strong);
                break;
            case CONTROLLER_TYPE_AndroidController:
                if (d->vendor_id == UNI_HID_PARSER_STADIA_VID && d->product_id == UNI_HID_PARSER_STADIA_PID)
                    uni_hid_parser_stadia_play_dual_rumble(d, 0, length, weak, strong);
                break;
            case CONTROLLER_TYPE_PSMoveController:
                uni_hid_parser_psmove_play_dual_rumble(d, 0, length, weak, strong);
                break;
            case CONTROLLER_TYPE_PS3Controller:
                uni_hid_parser_ds3_play_dual_rumble(d, 0, length, weak, strong);
                break;
            case CONTROLLER_TYPE_PS4Controller:
                uni_hid_parser_ds4_play_dual_rumble(d, 0, length, weak, strong);
                break;
            case CONTROLLER_TYPE_PS5Controller:
                uni_hid_parser_ds5_play_dual_rumble(d, 0, length, weak, strong);
                break;
            case CONTROLLER_TYPE_WiiController:
                uni_hid_parser_wii_play_dual_rumble(d, 0, length, weak, strong);
                break;
            case CONTROLLER_TYPE_SwitchProController:
            case CONTROLLER_TYPE_SwitchJoyConRight:
            case CONTROLLER_TYPE_SwitchJoyConLeft:
                /* Outlive the feedback period so a long rumble isn't stopped and restarted on
                 * every cycle (Gamepad/RumbleTiming.h). */
                uni_hid_parser_switch_play_dual_rumble(d, 0, switch_rumble::kRumbleDurationMs, weak, strong);
                break;
            case CONTROLLER_TYPE_Switch2ProController:
            case CONTROLLER_TYPE_Switch2JoyConRight:
            case CONTROLLER_TYPE_Switch2JoyConLeft:
                uni_hid_parser_switch2_play_dual_rumble(d, 0, length, weak, strong);
                break;
            case CONTROLLER_TYPE_SteamControllerTriton:
                uni_hid_parser_steam_triton_play_dual_rumble(d, 0, length, weak, strong);
                break;
            default:
                break;
        }
    }

    inline uni_hid_device_t* partner_device(uni_hid_device_t* d)
    {
        const int idx = pair_partner_idx(d);
        uni_hid_device_t* partner = (idx >= 0 && idx < CONFIG_BLUEPAD32_MAX_DEVICES)
                                        ? uni_hid_device_get_instance_for_idx(idx)
                                        : nullptr;
        return partner == d ? nullptr : partner;
    }

    /* The host's rumble for a pad, played on its Joy-Con partner too (called for the pair's
     * primary half; the secondary is skipped). A merged Joy-Con pair follows the dongle option:
     * per side (as SDL / Steam / Linux drive a pair: left motor on the left Joy-Con, right motor
     * on the right one) or both motors on both; a half with nothing to play is left alone (its
     * own rumble duration stops it). */
    inline void play_host_rumble(uni_hid_device_t* d, uint16_t length, uint8_t rumble_l, uint8_t rumble_r,
                                 bool joycon_pair_per_side)
    {
        if (is_joycon_pair_secondary(d))
            return;
        uni_hid_device_t* partner = partner_device(d);
        if (partner && is_switch_joycon(d)) {
            for (uni_hid_device_t* half : {d, partner}) {
                const auto h = joycon_settings::pair_half_rumble(
                    half->controller_type == CONTROLLER_TYPE_SwitchJoyConLeft, joycon_pair_per_side,
                    rumble_l, rumble_r);
                if (h.weak || h.strong)
                    uni_hid_parser_switch_play_dual_rumble(half, 0, switch_rumble::kRumbleDurationMs,
                                                           h.weak, h.strong);
            }
            return;
        }
        play_rumble(d, length, rumble_l, rumble_r);
        if (partner)
            play_rumble(partner, length, rumble_l, rumble_r);
    }

    /* The output mode's lightbar colour goes on a while after connecting (Board/ModeIndicator.h),
     * deferred like the connection rumble: early output reports can drop a fresh DS4. */
    constexpr uint32_t kModeLightbarDelayMs = 1500;

    inline bool has_lightbar(const uni_hid_device_t* d)
    {
        return d->report_parser.set_lightbar_color != nullptr &&
               (d->controller_type == CONTROLLER_TYPE_PS4Controller ||
                d->controller_type == CONTROLLER_TYPE_PS5Controller);
    }

    /* Send `want` to the pad's lightbar unless it already shows it (`have`: last colour sent). */
    inline void set_lightbar(uni_hid_device_t* d, const Lightbar& want, Lightbar& have)
    {
        if (!want.valid || !has_lightbar(d))
            return;
        if (have.valid && have.same_colour(want))
            return;
        d->report_parser.set_lightbar_color(d, want.r, want.g, want.b);
        have = want;
    }

    /* Last BLE keepalive sent to a pad (platform time, ms); zeroed when the pad is ready, so the
     * first one goes out on the next feedback tick. */
    struct KeepaliveState
    {
        uint32_t xbox_last_ms{0};
        uint32_t switch2_last_ms{0};
    };

    /* Called every feedback tick with whether the host is rumbling this pad. */
    inline void ble_keepalive(uni_hid_device_t* d, bool rumbling, uint32_t now_ms, KeepaliveState& state)
    {
        if (rumbling)
            return;
        if (d->controller_type == CONTROLLER_TYPE_XBoxOneController && d->hids_cid != 0 &&
            (state.xbox_last_ms == 0 || now_ms - state.xbox_last_ms >= kXboxBleKeepaliveMs)) {
            uni_hid_parser_xboxone_ble_keepalive(d);
            state.xbox_last_ms = now_ms;
        }
        if (uni_hid_parser_switch2_is_ble_device(d) && uni_hid_parser_switch2_is_ready(d) &&
            !uni_hid_parser_switch2_is_joycon_pair_secondary(d) &&
            !uni_hid_parser_switch2_keepalive_timer_active(d) &&
            (state.switch2_last_ms == 0 || now_ms - state.switch2_last_ms >= kSwitch2BleKeepaliveMs)) {
            uni_hid_parser_switch2_send_keepalive(d);
            state.switch2_last_ms = now_ms;
        }
    }

    inline bool disconnect_combo_pressed(const uni_hid_device_t* d, const uni_gamepad_t* gp)
    {
        if (d->controller_type == CONTROLLER_TYPE_OUYAController)
            return (gp->buttons & BUTTON_THUMB_L) && (gp->buttons & BUTTON_THUMB_R);
        return (gp->misc_buttons & MISC_BUTTON_START) && (gp->misc_buttons & MISC_BUTTON_BACK);
    }

    /* Custom: Bluepad32 5 reports motion in SI units (m/s^2 with gravity, rad/s) in one Y-up frame
     * for every pad (X right, Y up out of the face, Z towards the player). The adapter's motion
     * code (MotionImu, the idle turn-off, the hybrid's SET_PAD_EXT) works in each pad's own units
     * and frame, as Bluepad32 gave them before: back to those, undoing what each parser does:
     *  - DS4 / DualSense: 8192 per g, 1024 per deg/s, the same axes;
     *  - Switch Pro / Joy-Cons: the Pro's axes (X forward, Y left, Z up; the right Joy-Con already
     *    aligned with them), 4096 per g, gyro in raw counts * 1000 at the factory scale (13371
     *    counts per 936 deg/s);
     *  - Wiimote: counts from 0x200, 104 per g, its own axes;
     *  - Switch 2 and Steam Controller 2026 (MegaCadeDev's parsers, kept): still raw counts;
     *  - any other pad (DS3, PS Move, Steam Controller: no motion source here): DS4 units, for the
     *    idle turn-off only. */
    struct RawMotion {
        int32_t accel[3];
        int32_t gyro[3];
    };

    inline int32_t round_motion(float v)
    {
        return static_cast<int32_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
    }

    inline RawMotion raw_motion(const uni_hid_device_t* d, const uni_gamepad_t* gp)
    {
        constexpr float kPerG = 1.0f / UNI_STANDARD_GRAVITY;
        constexpr float kDegPerRad = 1.0f / UNI_DEG_TO_RAD;
        RawMotion m{};
        const float* a = gp->accel;
        const float* g = gp->gyro;
        switch (d->controller_type) {
            case CONTROLLER_TYPE_SwitchProController:
            case CONTROLLER_TYPE_SwitchJoyConLeft:
            case CONTROLLER_TYPE_SwitchJoyConRight: {
                // Y-up (x, y, z) = Pro (-y, z, -x)
                const float pro_a[3] = {-a[2], -a[0], a[1]};
                const float pro_g[3] = {-g[2], -g[0], g[1]};
                for (int i = 0; i < 3; ++i) {
                    m.accel[i] = round_motion(pro_a[i] * kPerG * 4096.0f);
                    m.gyro[i] = round_motion(pro_g[i] * kDegPerRad * (13371.0f * 1000.0f / 936.0f));
                }
                break;
            }
            case CONTROLLER_TYPE_WiiController: {
                // Y-up (x, y, z) = Wiimote (-x, z, -y)
                const float wii[3] = {-a[0], -a[2], a[1]};
                for (int i = 0; i < 3; ++i)
                    m.accel[i] = round_motion(wii[i] * kPerG * 104.0f);
                break;
            }
            case CONTROLLER_TYPE_Switch2ProController:
            case CONTROLLER_TYPE_Switch2JoyConLeft:
            case CONTROLLER_TYPE_Switch2JoyConRight:
            case CONTROLLER_TYPE_SteamControllerTriton:
                for (int i = 0; i < 3; ++i) {
                    m.accel[i] = round_motion(a[i]);
                    m.gyro[i] = round_motion(g[i]);
                }
                break;
            default:  // DS4 / DualSense units
                for (int i = 0; i < 3; ++i) {
                    m.accel[i] = round_motion(a[i] * kPerG * 8192.0f);
                    m.gyro[i] = round_motion(g[i] * kDegPerRad * 1024.0f);
                }
                break;
        }
        return m;
    }

    /* Where the pad's motion comes from (Gamepad/MotionSource.h); NONE without sensors. A Wiimote
     * counts once it reports accelerometer data (accel mode, or readings above the noise). */
    inline uint8_t motion_source_of(const uni_hid_device_t* d, const uni_gamepad_t* gp)
    {
        switch (d->controller_type) {
            case CONTROLLER_TYPE_PS4Controller:
                return motion_source::DS4;
            case CONTROLLER_TYPE_PS5Controller:
                return motion_source::DS5;
            case CONTROLLER_TYPE_SwitchProController:
            case CONTROLLER_TYPE_Switch2ProController:
            /* Joy-Cons too: the parser aligns the right Joy-Con's axes with the left / Pro ones,
             * and a merged pair carries one half's IMU. */
            case CONTROLLER_TYPE_SwitchJoyConLeft:
            case CONTROLLER_TYPE_SwitchJoyConRight:
                return motion_source::SWITCH_PRO;
            case CONTROLLER_TYPE_WiiController: {
                if (d->controller_subtype == CONTROLLER_SUBTYPE_WIIMOTE_ACCEL ||
                    d->controller_subtype == CONTROLLER_SUBTYPE_WIIMOTE_NUNCHUK_ACCEL)
                    return motion_source::WII_BT;
                const RawMotion m = raw_motion(d, gp);
                const int32_t l1 = (m.accel[0] >= 0 ? m.accel[0] : -m.accel[0]) +
                                   (m.accel[1] >= 0 ? m.accel[1] : -m.accel[1]) +
                                   (m.accel[2] >= 0 ? m.accel[2] : -m.accel[2]);
                return l1 > 16 ? motion_source::WII_BT : motion_source::NONE;
            }
            default:
                return motion_source::NONE;
        }
    }

    /* A DualSense's own input for the STEAM mode passthrough (Gamepad/Ps5Core.h). */
    inline Ps5Core ps5_core_from(const uni_hid_device_t* d, const uni_gamepad_t* gp)
    {
        auto stick = [](int32_t axis) {
            const int32_t b = axis / 4 + 127;
            return static_cast<uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b));
        };
        auto trigger = [](int32_t value) {
            const int32_t b = value >> 2;
            return static_cast<uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b));
        };
        Ps5Core core;
        core.joystick_lx = stick(gp->axis_x);
        core.joystick_ly = stick(gp->axis_y);
        core.joystick_rx = stick(gp->axis_rx);
        core.joystick_ry = stick(gp->axis_ry);
        /* Custom fix: the triggers as the mapped input below takes them (the STEAM report uses
         * these): digital ones (Joy-Con / Switch Pro ZL / ZR) at full scale, where they read 0;
         * none while a Wii pad's shoulder is held (its Z reports brake / throttle as LB / RB). */
        if (!(is_wii(d) && (gp->buttons & (BUTTON_SHOULDER_L | BUTTON_SHOULDER_R)))) {
            core.trigger_l = trigger(gp->brake);
            core.trigger_r = trigger(gp->throttle);
            if (core.trigger_l == 0 && (gp->buttons & BUTTON_TRIGGER_L)) core.trigger_l = 0xFF;
            if (core.trigger_r == 0 && (gp->buttons & BUTTON_TRIGGER_R)) core.trigger_r = 0xFF;
        }

        uint8_t b0 = ps5_bits::DPAD_CENTER;
        switch (gp->dpad) {
            case DPAD_UP:                b0 = ps5_bits::DPAD_UP;         break;
            case DPAD_DOWN:              b0 = ps5_bits::DPAD_DOWN;       break;
            case DPAD_LEFT:              b0 = ps5_bits::DPAD_LEFT;       break;
            case DPAD_RIGHT:             b0 = ps5_bits::DPAD_RIGHT;      break;
            case DPAD_UP | DPAD_RIGHT:   b0 = ps5_bits::DPAD_UP_RIGHT;   break;
            case DPAD_DOWN | DPAD_RIGHT: b0 = ps5_bits::DPAD_RIGHT_DOWN; break;
            case DPAD_DOWN | DPAD_LEFT:  b0 = ps5_bits::DPAD_DOWN_LEFT;  break;
            case DPAD_UP | DPAD_LEFT:    b0 = ps5_bits::DPAD_LEFT_UP;    break;
            default: break;
        }
        if (gp->buttons & BUTTON_X) b0 |= ps5_bits::SQUARE;
        if (gp->buttons & BUTTON_A) b0 |= ps5_bits::CROSS;
        if (gp->buttons & BUTTON_B) b0 |= ps5_bits::CIRCLE;
        if (gp->buttons & BUTTON_Y) b0 |= ps5_bits::TRIANGLE;
        core.buttons[0] = b0;

        uint8_t b1 = 0;
        if (gp->buttons & BUTTON_SHOULDER_L) b1 |= ps5_bits::L1;
        if (gp->buttons & BUTTON_SHOULDER_R) b1 |= ps5_bits::R1;
        if (core.trigger_l) b1 |= ps5_bits::L2;  // as a DualSense sets them (PS4 / PS3 outputs alike)
        if (core.trigger_r) b1 |= ps5_bits::R2;
        if (gp->misc_buttons & MISC_BUTTON_BACK) b1 |= ps5_bits::SHARE;
        if (gp->misc_buttons & MISC_BUTTON_START) b1 |= ps5_bits::OPTIONS;
        if (gp->buttons & BUTTON_THUMB_L) b1 |= ps5_bits::L3;
        if (gp->buttons & BUTTON_THUMB_R) b1 |= ps5_bits::R3;
        core.buttons[1] = b1;

        uint8_t b2 = 0;
        if (gp->misc_buttons & MISC_BUTTON_SYSTEM) b2 |= ps5_bits::PS_HOME;
        if (gp->misc_buttons & MISC_BUTTON_CAPTURE) b2 |= ps5_bits::MUTE;
        core.buttons[2] = b2;
        return core;
    }

    /* A pad's input after its user profile, in the output's terms (GamepadBits). */
    struct Input
    {
        uint8_t dpad{0};
        uint16_t buttons{0};
        uint8_t trigger_l{0};
        uint8_t trigger_r{0};
        int16_t joystick_lx{0};
        int16_t joystick_ly{0};
        int16_t joystick_rx{0};
        int16_t joystick_ry{0};
        uint8_t touch_raw[8]{};      // DS4 / DualSense touch points, as the parser reports them
        bool touchpad_valid{false};  // touch_raw / touchpad_click come from this report
        bool touchpad_click{false};
    };

    /* Bluepad32's gamepad state mapped through the user profile. */
    inline Input map_input(uni_hid_device_t* d, const uni_gamepad_t* gp, const GamepadMapper& map)
    {
        Input in;
        const bool wii = is_wii(d);

        switch (gp->dpad) {
            case DPAD_UP:                 in.dpad = map.MAP_DPAD_UP;         break;
            case DPAD_DOWN:               in.dpad = map.MAP_DPAD_DOWN;       break;
            case DPAD_LEFT:               in.dpad = map.MAP_DPAD_LEFT;       break;
            case DPAD_RIGHT:              in.dpad = map.MAP_DPAD_RIGHT;      break;
            case DPAD_UP | DPAD_RIGHT:    in.dpad = map.MAP_DPAD_UP_RIGHT;   break;
            case DPAD_DOWN | DPAD_RIGHT:  in.dpad = map.MAP_DPAD_DOWN_RIGHT; break;
            case DPAD_DOWN | DPAD_LEFT:   in.dpad = map.MAP_DPAD_DOWN_LEFT;  break;
            case DPAD_UP | DPAD_LEFT:     in.dpad = map.MAP_DPAD_UP_LEFT;    break;
            default: break;
        }

        if (gp->buttons & BUTTON_A)          in.buttons |= map.MAP_BUTTON_A;
        if (gp->buttons & BUTTON_B)          in.buttons |= map.MAP_BUTTON_B;
        if (gp->buttons & BUTTON_X)          in.buttons |= map.MAP_BUTTON_X;
        if (gp->buttons & BUTTON_Y)          in.buttons |= map.MAP_BUTTON_Y;
        if (gp->buttons & BUTTON_SHOULDER_L) in.buttons |= map.MAP_BUTTON_LB;
        if (gp->buttons & BUTTON_SHOULDER_R) in.buttons |= map.MAP_BUTTON_RB;
        if (!wii) {  // a Wii pad's thumb bits are not L3 / R3
            if (gp->buttons & BUTTON_THUMB_L) in.buttons |= map.MAP_BUTTON_L3;
            if (gp->buttons & BUTTON_THUMB_R) in.buttons |= map.MAP_BUTTON_R3;
        }
        if (gp->misc_buttons & MISC_BUTTON_BACK)   in.buttons |= map.MAP_BUTTON_BACK;
        if (gp->misc_buttons & MISC_BUTTON_START)  in.buttons |= map.MAP_BUTTON_START;
        if (gp->misc_buttons & MISC_BUTTON_SYSTEM) in.buttons |= map.MAP_BUTTON_SYS;
        /* Capture (Switch pads) is the Misc button. On a DualSense, MISC_BUTTON_CAPTURE is the mute
         * button: left out (it switches the adaptive triggers, Bluepad32.cpp). */
        if ((gp->misc_buttons & MISC_BUTTON_CAPTURE) && d->controller_type != CONTROLLER_TYPE_PS5Controller)
            in.buttons |= map.MAP_BUTTON_MISC;

        /* Analog triggers when present, else the digital trigger buttons (e.g. Wii U Pro LT / RT)
         * at full scale. A Wii pad's Z reports brake / throttle too but only stands for LB / RB:
         * no trigger while a shoulder button is held there. */
        const bool wii_shoulder_pressed = wii && (gp->buttons & (BUTTON_SHOULDER_L | BUTTON_SHOULDER_R));
        if (!wii_shoulder_pressed) {
            in.trigger_l = map.scale_trigger_l<10>(static_cast<uint16_t>(gp->brake));
            in.trigger_r = map.scale_trigger_r<10>(static_cast<uint16_t>(gp->throttle));
            if (in.trigger_l == 0 && (gp->buttons & BUTTON_TRIGGER_L))
                in.trigger_l = 0xFF;
            if (in.trigger_r == 0 && (gp->buttons & BUTTON_TRIGGER_R))
                in.trigger_r = 0xFF;
        }

        /* The Nunchuk stick is Bluepad32's right axis: the left stick, for character movement. */
        if (has_nunchuk(d)) {
            std::tie(in.joystick_lx, in.joystick_ly) = map.scale_joystick_l<10>(gp->axis_rx, gp->axis_ry);
        } else {
            std::tie(in.joystick_lx, in.joystick_ly) = map.scale_joystick_l<10>(gp->axis_x, gp->axis_y);
            std::tie(in.joystick_rx, in.joystick_ry) = map.scale_joystick_r<10>(gp->axis_rx, gp->axis_ry);
        }

        /* DS4 / DualSense touchpad. Its press goes out as the touchpad's own (PS4, STEAM, Mouse +
         * Keyboard, the web app) and as the Misc button only on outputs without a touchpad
         * (GamepadMapper::touchpad_as_misc): in STEAM mode it was the touchpad click and Mute. */
        if (d->controller_type == CONTROLLER_TYPE_PS4Controller ||
            d->controller_type == CONTROLLER_TYPE_PS5Controller) {
            bool click = false;
            if (d->controller_type == CONTROLLER_TYPE_PS5Controller)
                uni_hid_parser_ds5_get_touchpad(d, in.touch_raw, &click);
            else
                uni_hid_parser_ds4_get_touchpad(d, in.touch_raw, &click);
            in.touchpad_valid = true;
            in.touchpad_click = click;
            if (click && map.touchpad_as_misc())
                in.buttons |= map.MAP_BUTTON_MISC;
        }
        return in;
    }

} // namespace bt_pad

#endif // _OGXM_BT_PAD_H_
