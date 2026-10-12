#include "UserSettings/TriggerSettings.h"

bool TriggerSettings::is_same(const TriggerSettingsRaw& raw) const
{
    return  dz_inner == Fix16(raw.dz_inner) &&
            dz_outer == Fix16(raw.dz_outer) &&
            anti_dz_inner == Fix16(raw.anti_dz_inner) &&
            anti_dz_outer == Fix16(raw.anti_dz_outer) &&
            curve == Fix16(raw.curve);
}

void TriggerSettings::set_from_raw(const TriggerSettingsRaw& raw)
{
    dz_inner = Fix16(raw.dz_inner);
    dz_outer = Fix16(raw.dz_outer);
    anti_dz_inner = Fix16(raw.anti_dz_inner);
    anti_dz_outer = Fix16(raw.anti_dz_outer);
    curve = Fix16(raw.curve);
}

bool TriggerSettings::raw_has_customization(const TriggerSettingsRaw& raw)
{
    static const TriggerSettingsRaw defaults{};
    return  raw.dz_inner != defaults.dz_inner ||
            raw.dz_outer != defaults.dz_outer ||
            raw.anti_dz_inner != defaults.anti_dz_inner ||
            raw.anti_dz_outer != defaults.anti_dz_outer ||
            raw.curve != defaults.curve;
}
