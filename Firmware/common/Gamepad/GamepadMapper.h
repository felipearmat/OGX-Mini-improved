#ifndef _GAMEPAD_MAPPER_H_
#define _GAMEPAD_MAPPER_H_

#include <cstdint>
#include <type_traits>
#include <utility>

#include "libfixmath/fix16.hpp"

#include "Gamepad/GamepadBits.h"
#include "Gamepad/Range.h"
#include "Gamepad/fix16ext.h"
#include "UserSettings/UserProfile.h"
#include "UserSettings/JoystickSettings.h"
#include "UserSettings/TriggerSettings.h"
#include "USBDevice/DeviceDriver/DeviceDriverTypes.h"

/* A pad's user profile applied to its input: button / D-pad remapping (MAP_*), stick and trigger
 * settings. Shared by the RP2040 firmware (base of Gamepad) and the ESP32 companion of the hybrid,
 * so a profile feels the same on every board. */
class GamepadMapper : public GamepadBits
{
public:
    //Mappings used by host to set buttons

    uint8_t MAP_DPAD_UP         = DPAD_UP        ;
    uint8_t MAP_DPAD_DOWN       = DPAD_DOWN      ;
    uint8_t MAP_DPAD_LEFT       = DPAD_LEFT      ;
    uint8_t MAP_DPAD_RIGHT      = DPAD_RIGHT     ;
    uint8_t MAP_DPAD_UP_LEFT    = DPAD_UP_LEFT   ;
    uint8_t MAP_DPAD_UP_RIGHT   = DPAD_UP_RIGHT  ;
    uint8_t MAP_DPAD_DOWN_LEFT  = DPAD_DOWN_LEFT ;
    uint8_t MAP_DPAD_DOWN_RIGHT = DPAD_DOWN_RIGHT;
    uint8_t MAP_DPAD_NONE       = DPAD_NONE      ;

    uint16_t MAP_BUTTON_A     = BUTTON_A    ;
    uint16_t MAP_BUTTON_B     = BUTTON_B    ;
    uint16_t MAP_BUTTON_X     = BUTTON_X    ;
    uint16_t MAP_BUTTON_Y     = BUTTON_Y    ;
    uint16_t MAP_BUTTON_L3    = BUTTON_L3   ;
    uint16_t MAP_BUTTON_R3    = BUTTON_R3   ;
    uint16_t MAP_BUTTON_BACK  = BUTTON_BACK ;
    uint16_t MAP_BUTTON_START = BUTTON_START;
    uint16_t MAP_BUTTON_LB    = BUTTON_LB   ;
    uint16_t MAP_BUTTON_RB    = BUTTON_RB   ;
    uint16_t MAP_BUTTON_SYS   = BUTTON_SYS  ;
    uint16_t MAP_BUTTON_MISC  = BUTTON_MISC ;

    uint8_t MAP_ANALOG_OFF_UP    = ANALOG_OFF_UP   ;
    uint8_t MAP_ANALOG_OFF_DOWN  = ANALOG_OFF_DOWN ;
    uint8_t MAP_ANALOG_OFF_LEFT  = ANALOG_OFF_LEFT ;
    uint8_t MAP_ANALOG_OFF_RIGHT = ANALOG_OFF_RIGHT;
    uint8_t MAP_ANALOG_OFF_A     = ANALOG_OFF_A    ;
    uint8_t MAP_ANALOG_OFF_B     = ANALOG_OFF_B    ;
    uint8_t MAP_ANALOG_OFF_X     = ANALOG_OFF_X    ;
    uint8_t MAP_ANALOG_OFF_Y     = ANALOG_OFF_Y    ;
    uint8_t MAP_ANALOG_OFF_LB    = ANALOG_OFF_LB   ;
    uint8_t MAP_ANALOG_OFF_RB    = ANALOG_OFF_RB   ;

    void set_profile(const UserProfile& user_profile, DeviceDriverType driver = DeviceDriverType::NONE) 
    {
        /* Custom: with the web app open the controller's own input goes out (live input, mapping
         * rows that light up): the default mapping, and since 2026-10-23 no stick / trigger
         * settings either, so the web app's "Preview" applies the settings being edited once
         * instead of on top of the saved ones. */
        const bool web_app = driver == DeviceDriverType::WEBAPP;
        set_profile_mappings(web_app ? UserProfile() : user_profile);
        set_profile_settings(web_app ? UserProfile() : user_profile, driver);
    }

    template <uint8_t bits = 0, typename T>
    inline std::pair<int16_t, int16_t> scale_joystick_r(T x, T y, bool invert_y = false) const
    {
        int16_t joy_x = 0;
        int16_t joy_y = 0;
        if constexpr (bits > 0)
        {
            joy_x = Range::scale_from_bits<int16_t, bits>(x);
            joy_y = Range::scale_from_bits<int16_t, bits>(y);
        }
        else if constexpr (!std::is_same_v<T, int16_t>)
        {
            joy_x = Range::scale<int16_t>(x);
            joy_y = Range::scale<int16_t>(y);
        }
        else
        {
            joy_x = x;
            joy_y = y;
        }

        return  joy_settings_r_en_ 
                    ? apply_joystick_settings(joy_x, joy_y, joy_settings_r_, invert_y) 
                    : std::make_pair(joy_x, invert_y ? Range::invert(joy_y) : joy_y);
    }

    template <uint8_t bits = 0, typename T>
    inline std::pair<int16_t, int16_t> scale_joystick_l(T x, T y, bool invert_y = false) const
    {
        int16_t joy_x = 0;
        int16_t joy_y = 0;
        if constexpr (bits > 0)
        {
            joy_x = Range::scale_from_bits<int16_t, bits>(x);
            joy_y = Range::scale_from_bits<int16_t, bits>(y);
        }
        else if constexpr (!std::is_same_v<T, int16_t>)
        {
            joy_x = Range::scale<int16_t>(x);
            joy_y = Range::scale<int16_t>(y);
        }
        else
        {
            joy_x = x;
            joy_y = y;
        }

        return  joy_settings_l_en_ 
                    ? apply_joystick_settings(joy_x, joy_y, joy_settings_l_, invert_y) 
                    : std::make_pair(joy_x, invert_y ? Range::invert(joy_y) : joy_y);
    }

    template <uint8_t bits = 0, typename T>
    inline uint8_t scale_trigger_l(T value) const
    {
        uint8_t trigger_value = 0;
        if constexpr (bits > 0)
        {
            trigger_value = Range::scale_from_bits<uint8_t, bits>(value);
        }
        else if constexpr (!std::is_same_v<T, uint8_t>)
        {
            trigger_value = Range::scale<uint8_t>(value);
        }
        else
        {
            trigger_value = value;
        }
        return  trig_settings_l_en_ 
                    ? apply_trigger_settings(trigger_value, trig_settings_l_) 
                    : trigger_value;
    }

    template <uint8_t bits = 0, typename T>
    inline uint8_t scale_trigger_r(T value) const
    {
        uint8_t trigger_value = 0;
        if constexpr (bits > 0)
        {
            trigger_value = Range::scale_from_bits<uint8_t, bits>(value);
        }
        else if constexpr (!std::is_same_v<T, uint8_t>)
        {
            trigger_value = Range::scale<uint8_t>(value);
        }
        else
        {
            trigger_value = value;
        }
        return  trig_settings_r_en_ 
                    ? apply_trigger_settings(trigger_value, trig_settings_r_) 
                    : trigger_value;
    }

protected:
    bool profile_analog_enabled_{false};

private:
    JoystickSettings joy_settings_l_;
    JoystickSettings joy_settings_r_;
    TriggerSettings trig_settings_l_;
    TriggerSettings trig_settings_r_;

    bool joy_settings_l_en_{false};
    bool joy_settings_r_en_{false};
    bool trig_settings_l_en_{false};
    bool trig_settings_r_en_{false};

    void set_profile_settings(const UserProfile& profile, DeviceDriverType driver = DeviceDriverType::NONE)
    {
        profile_analog_enabled_ = profile.analog_enabled ? true : false;

        const bool xinput_stock =
            (driver == DeviceDriverType::XINPUT);

        if (JoystickSettings::raw_has_customization(profile.joystick_settings_l))
        {
            joy_settings_l_en_ = true;
            if (!joy_settings_l_.is_same(profile.joystick_settings_l))
            {
                joy_settings_l_.set_from_raw(profile.joystick_settings_l);
                //This needs to be addressed in the webapp, just multiply here for now
                joy_settings_l_.axis_restrict *= static_cast<int16_t>(100);
                joy_settings_l_.angle_restrict *= static_cast<int16_t>(100);
                joy_settings_l_.anti_dz_angular *= static_cast<int16_t>(100);
            }
        }
        else if (xinput_stock)
        {
            joy_settings_l_.set_xbox360_stock_feel(false);
            joy_settings_l_en_ = true;
        }
        else
        {
            joy_settings_l_ = JoystickSettings{};
            joy_settings_l_en_ = false;
        }

        if (JoystickSettings::raw_has_customization(profile.joystick_settings_r))
        {
            joy_settings_r_en_ = true;
            if (!joy_settings_r_.is_same(profile.joystick_settings_r))
            {
                joy_settings_r_.set_from_raw(profile.joystick_settings_r);
                //This needs to be addressed in the webapp, just multiply here for now
                joy_settings_r_.axis_restrict *= static_cast<int16_t>(100);
                joy_settings_r_.angle_restrict *= static_cast<int16_t>(100);
                joy_settings_r_.anti_dz_angular *= static_cast<int16_t>(100);
            }
        }
        else if (xinput_stock)
        {
            joy_settings_r_.set_xbox360_stock_feel(true);
            joy_settings_r_en_ = true;
        }
        else
        {
            joy_settings_r_ = JoystickSettings{};
            joy_settings_r_en_ = false;
        }

        /* On when the profile shapes the trigger at all, so applying the same profile again (a
         * mode change) keeps it on. */
        trig_settings_l_en_ = TriggerSettings::raw_has_customization(profile.trigger_settings_l);
        trig_settings_l_.set_from_raw(profile.trigger_settings_l);
        trig_settings_r_en_ = TriggerSettings::raw_has_customization(profile.trigger_settings_r);
        trig_settings_r_.set_from_raw(profile.trigger_settings_r);

    }

    void set_profile_mappings(const UserProfile& profile)
    {
        MAP_DPAD_UP         = profile.dpad_up;
        MAP_DPAD_DOWN       = profile.dpad_down;
        MAP_DPAD_LEFT       = profile.dpad_left;
        MAP_DPAD_RIGHT      = profile.dpad_right;
        MAP_DPAD_UP_LEFT    = profile.dpad_up | profile.dpad_left;
        MAP_DPAD_UP_RIGHT   = profile.dpad_up | profile.dpad_right;
        MAP_DPAD_DOWN_LEFT  = profile.dpad_down | profile.dpad_left;
        MAP_DPAD_DOWN_RIGHT = profile.dpad_down | profile.dpad_right;
        MAP_DPAD_NONE       = 0;

        MAP_BUTTON_A     = profile.button_a;
        MAP_BUTTON_B     = profile.button_b;
        MAP_BUTTON_X     = profile.button_x;
        MAP_BUTTON_Y     = profile.button_y;
        MAP_BUTTON_L3    = profile.button_l3;
        MAP_BUTTON_R3    = profile.button_r3;
        MAP_BUTTON_BACK  = profile.button_back;
        MAP_BUTTON_START = profile.button_start;
        MAP_BUTTON_LB    = profile.button_lb;
        MAP_BUTTON_RB    = profile.button_rb;
        MAP_BUTTON_SYS   = profile.button_sys;
        MAP_BUTTON_MISC  = profile.button_misc;

        MAP_ANALOG_OFF_UP    = profile.analog_off_up;
        MAP_ANALOG_OFF_DOWN  = profile.analog_off_down;
        MAP_ANALOG_OFF_LEFT  = profile.analog_off_left;
        MAP_ANALOG_OFF_RIGHT = profile.analog_off_right;
        MAP_ANALOG_OFF_A     = profile.analog_off_a;
        MAP_ANALOG_OFF_B     = profile.analog_off_b;
        MAP_ANALOG_OFF_X     = profile.analog_off_x;
        MAP_ANALOG_OFF_Y     = profile.analog_off_y;
        MAP_ANALOG_OFF_LB    = profile.analog_off_lb;
        MAP_ANALOG_OFF_RB    = profile.analog_off_rb;
    }

    static inline std::pair<int16_t, int16_t> apply_joystick_settings(
        int16_t gp_joy_x, 
        int16_t gp_joy_y, 
        const JoystickSettings& set,
        bool invert_y)
    {
        static const Fix16 
            FIX_0(0.0f),
            FIX_1(1.0f),
            FIX_2(2.0f),
            FIX_45(45.0f),
            FIX_90(90.0f),
            FIX_100(100.0f),
            FIX_180(180.0f),
            FIX_EPSILON(0.0001f),
            FIX_EPSILON2(0.001f),
            FIX_ELLIPSE_DEF(1.570796f),
            FIX_DIAG_DIVISOR(0.29289f);

        Fix16 x = (set.invert_x ? Fix16(Range::invert(gp_joy_x)) : Fix16(gp_joy_x)) / Range::MAX<int16_t>;
        Fix16 y = ((set.invert_y ^ invert_y) ? Fix16(Range::invert(gp_joy_y)) : Fix16(gp_joy_y)) / Range::MAX<int16_t>;

        const Fix16 abs_x = fix16::abs(x);
        const Fix16 abs_y = fix16::abs(y);
        const Fix16 inv_axis_restrict = FIX_1 / (FIX_1 - set.axis_restrict);

        Fix16 rAngle = (abs_x < FIX_EPSILON) 
            ? FIX_90 
            : fix16::rad2deg(fix16::abs(fix16::atan(y / x)));

        Fix16 axial_x = (abs_x <= set.axis_restrict && rAngle > FIX_45) 
            ? FIX_0 
            : ((abs_x - set.axis_restrict) * inv_axis_restrict);
                
        Fix16 axial_y = (abs_y <= set.axis_restrict && rAngle <= FIX_45) 
            ? FIX_0 
            : ((abs_y - set.axis_restrict) * inv_axis_restrict);

        /* WebApp uses raw stick magnitude for the inner deadzone test (before axial restrict). */
        const Fix16 raw_magnitude = fix16::sqrt(fix16::sq(abs_x) + fix16::sq(abs_y));
        Fix16 in_magnitude = fix16::sqrt(fix16::sq(axial_x) + fix16::sq(axial_y));

        /* Anti-deadzone pushes any non-zero reading to at least anti_dz_* magnitude.
         * Switch Pro / Switch 2 Pro USB sticks are rarely perfect at the nominal
         * center, so with dz_inner=0 that becomes a constant drift (often bottom-left
         * after Y invert). Gate idle noise when anti-dz is active, but keep the
         * user's dz_inner for the magnitude warp so anti-dz still applies fully. */
        const bool anti_dz_active =
            set.anti_dz_square > FIX_EPSILON || set.anti_dz_circle > FIX_EPSILON ||
            set.anti_dz_square_y_scale > FIX_EPSILON || set.anti_dz_circle_y_scale > FIX_EPSILON;
        static const Fix16 FIX_ANTI_DZ_NOISE_MIN(0.07f);
        static const Fix16 FIX_ANTI_DZ_NOISE_MAX(0.12f);
        Fix16 gate = set.dz_inner;
        if (anti_dz_active && gate < FIX_ANTI_DZ_NOISE_MIN)
        {
            const Fix16 anti_ref =
                (set.anti_dz_circle > set.anti_dz_square) ? set.anti_dz_circle : set.anti_dz_square;
            Fix16 adaptive_gate = anti_ref * Fix16(0.4f);
            if (adaptive_gate < FIX_ANTI_DZ_NOISE_MIN)
            {
                adaptive_gate = FIX_ANTI_DZ_NOISE_MIN;
            }
            if (adaptive_gate > FIX_ANTI_DZ_NOISE_MAX)
            {
                adaptive_gate = FIX_ANTI_DZ_NOISE_MAX;
            }
            gate = adaptive_gate;
        }

        if (raw_magnitude <= gate)
        {
            return { 0, 0 };
        }

        const Fix16 dz_inner = set.dz_inner;

        Fix16 angle = 
            fix16::abs(axial_x) < FIX_EPSILON 
                ? FIX_90 
                : fix16::rad2deg(fix16::abs(fix16::atan(axial_y / axial_x)));

        Fix16 anti_r_scale = (set.anti_dz_square_y_scale == FIX_0) ? set.anti_dz_square : set.anti_dz_square_y_scale;
        Fix16 anti_dz_c = set.anti_dz_circle;

        if (set.anti_dz_circle_y_scale > FIX_0 && anti_dz_c > FIX_0)
        {
            Fix16 anti_ellip_scale = set.anti_dz_circle_y_scale / anti_dz_c;
            Fix16 ellipse_angle = fix16::atan((FIX_1 / anti_ellip_scale) * fix16::tan(fix16::deg2rad(rAngle)));
            ellipse_angle = (ellipse_angle < FIX_0) ? FIX_ELLIPSE_DEF : ellipse_angle;

            Fix16 ellipse_x = fix16::cos(ellipse_angle);
            Fix16 ellipse_y = fix16::sqrt(fix16::sq(anti_ellip_scale) * (FIX_1 - fix16::sq(ellipse_x)));
            anti_dz_c *= fix16::sqrt(fix16::sq(ellipse_x) + fix16::sq(ellipse_y));
        }

        /* Match WebApp: counter square anti-dz shrinkage (must use anti_dz_square, not circle). */
        if (anti_dz_c > FIX_0)
        {
            anti_dz_c = anti_dz_c / ((anti_dz_c * (FIX_1 - set.anti_dz_square / set.dz_outer)) /
                                    (anti_dz_c * (FIX_1 - set.anti_dz_square)));
        }

        if (abs_x > set.axis_restrict && abs_y > set.axis_restrict)
        {
            const Fix16 FIX_ANGLE_MAX = set.angle_restrict / 2.0f;

            if (angle > FIX_0 && angle < FIX_ANGLE_MAX)
            {
                angle = FIX_0;
            }
            if (angle > (FIX_90 - FIX_ANGLE_MAX))
            {
                angle = FIX_90;
            }
            if (angle > FIX_ANGLE_MAX && angle < (FIX_90 - FIX_ANGLE_MAX))
            {
                angle = ((angle - FIX_ANGLE_MAX) * FIX_90) / ((FIX_90 - FIX_ANGLE_MAX) - FIX_ANGLE_MAX);
            }
        }

        Fix16 ref_angle = (angle < FIX_EPSILON2) ? FIX_0 : angle;
        Fix16 diagonal = (angle > FIX_45) ? (((angle - FIX_45) * (-FIX_45)) / FIX_45) + FIX_45 : angle;

        const Fix16 angle_comp = set.angle_restrict / FIX_2;

        if (angle < FIX_90 && angle > FIX_0)
        {
            angle = ((angle * ((FIX_90 - angle_comp) - angle_comp)) / FIX_90) + angle_comp;
        }

        if (axial_x < FIX_0 && axial_y > FIX_0)
        {
            angle = -angle;
        }
        if (axial_x > FIX_0 && axial_y < FIX_0)
        {
            angle = angle - FIX_180;
        }
        if (axial_x < FIX_0 && axial_y < FIX_0)
        {
            angle = angle + FIX_180;
        }

        //Deadzone Warp
        Fix16 out_magnitude = (in_magnitude - dz_inner) / (set.anti_dz_outer - dz_inner);
        out_magnitude = fix16::pow(out_magnitude, (FIX_1 / set.curve)) * (set.dz_outer - anti_dz_c) + anti_dz_c;
        out_magnitude = (out_magnitude > set.dz_outer && !set.uncap_radius) ? set.dz_outer : out_magnitude;

		Fix16 d_scale = (((out_magnitude - anti_dz_c) * (set.diag_scale_max - set.diag_scale_min)) / (set.dz_outer - anti_dz_c)) + set.diag_scale_min;		
		Fix16 c_scale = (diagonal * (FIX_1 / fix16::sqrt(FIX_2))) / FIX_45; //Both these lines scale the intensity of the warping
		c_scale       = FIX_1 - fix16::sqrt(FIX_1 - c_scale * c_scale);     //based on a circular curve to the perfect diagonal
		d_scale       = (c_scale * (d_scale - FIX_1)) / FIX_DIAG_DIVISOR + FIX_1;

		out_magnitude = out_magnitude * d_scale;

		//Scaling values for square antideadzone
		Fix16 new_x = fix16::cos(fix16::deg2rad(angle)) * out_magnitude;
		Fix16 new_y = fix16::sin(fix16::deg2rad(angle)) * out_magnitude;
		
		//Magic angle wobble fix by user ME.
		// if (angle > 45.0 && angle < 225.0) {
		// 	newX = inv(Math.sin(deg2rad(angle - 90.0)))*outputMagnitude;
		// 	newY = inv(Math.cos(deg2rad(angle - 270.0)))*outputMagnitude;
		// }
		
		//Square antideadzone scaling
		Fix16 output_x = fix16::abs(new_x) * (FIX_1 - set.anti_dz_square / set.dz_outer) + set.anti_dz_square;
		if (x < FIX_0)
        {
            output_x = -output_x;
        }
		if (ref_angle == FIX_90)
        {
            output_x = FIX_0;
        }
		
		Fix16 output_y = fix16::abs(new_y) * (FIX_1 - anti_r_scale / set.dz_outer) + anti_r_scale;
		if (y < FIX_0)
        {
            output_y = -output_y;
        }
		if (ref_angle == FIX_0)
        {
            output_y = FIX_0;
        }

        output_x = fix16::clamp(output_x, -FIX_1, FIX_1) * Range::MAX<int16_t>;
        output_y = fix16::clamp(output_y, -FIX_1, FIX_1) * Range::MAX<int16_t>;

        return { static_cast<int16_t>(fix16_to_int(output_x)), static_cast<int16_t>(fix16_to_int(output_y)) };
    }

    uint8_t apply_trigger_settings(uint8_t value, const TriggerSettings& set) const
    {
        Fix16 abs_value = fix16::abs(Fix16(static_cast<int16_t>(value)) / static_cast<int16_t>(Range::MAX<uint8_t>));

        if (abs_value < set.dz_inner)
        {
            return 0;
        }

        static const Fix16 
            FIX_0(0.0f),
            FIX_1(1.0f),
            FIX_2(2.0f);

        Fix16 value_out = (abs_value - set.dz_inner) / (set.anti_dz_outer - set.dz_inner);
        value_out = fix16::clamp(value_out, FIX_0, FIX_1);

        if (set.anti_dz_inner > FIX_0)
        {
            value_out = set.anti_dz_inner + (FIX_1 - set.anti_dz_inner) * value_out;
        }
        if (set.curve != FIX_1)
        {
            value_out = fix16::pow(value_out, FIX_1 / set.curve);
        }
        if (set.anti_dz_outer < FIX_1)
        {
            value_out = fix16::clamp(value_out * (FIX_1 / (FIX_1 - set.anti_dz_outer)), FIX_0, FIX_1);
        }

        value_out *= set.dz_outer;
        return static_cast<uint8_t>(fix16_to_int(value_out * static_cast<int16_t>(Range::MAX<uint8_t>)));
    }
}; // class GamepadMapper

#endif // _GAMEPAD_MAPPER_H_
