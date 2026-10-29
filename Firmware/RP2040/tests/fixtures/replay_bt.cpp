// The Bluetooth half of the fixture replay (fixture_replay.h).
#include <algorithm>
#include <array>

extern "C" {
#include "fake_bt.h"
#include "parser/uni_hid_parser_ds4.h"
#include "parser/uni_hid_parser_switch.h"
}
#include "Bluepad32/BtPadIn.h"
#include "fixture_replay.h"

namespace {

    struct Context {
        const Gamepad* gamepad;
        std::vector<ReplayFrame>* out;
        const FixtureRow* row;
        MotionImu::WiiPseudoGyroState wii{};
    };
    Context s_ctx;

    void push_frame(uni_hid_device_t* d)
    {
        uni_gamepad_t* gp = &d->controller.gamepad;
        ReplayFrame f{s_ctx.row->t, &s_ctx.row->label,
                      bt_pad::pad_in_from(d, gp, *s_ctx.gamepad, s_ctx.wii, s_ctx.row->t),
                      bt_pad::ps5_core_from(d, gp)};
        s_ctx.out->push_back(f);
    }

    /* Bluepad32's platform callback: for a Joy-Con pair it comes once per half's report, always
     * with the primary half and the merged state, as on the adapter. */
    void on_controller(uni_hid_device_t* d)
    {
        if (s_ctx.row != nullptr && !bt_pad::is_joycon_pair_secondary(d))
            push_frame(d);
    }

    /* A Joy-Con 0x30 report's stick (12-bit x, y), left half at byte 6, right at 9. */
    std::array<int, 2> joycon_stick(const std::vector<uint8_t>& r, bool left)
    {
        const size_t at = left ? 6 : 9;
        return {r[at] | ((r[at + 1] & 0x0F) << 8), (r[at + 1] >> 4) | (r[at + 2] << 4)};
    }

    void pack12(uint8_t* p, int x, int y)
    {
        p[0] = x & 0xFF;
        p[1] = ((x >> 8) & 0x0F) | ((y & 0x0F) << 4);
        p[2] = (y >> 4) & 0xFF;
    }

    /* The recordings were made with the pads already connected, so the SPI calibration the
     * adapter read is not in them (docs/TODO.md: the setup recording). Until then the factory
     * stick calibration is the recording's own: centre = median at rest, range = the extremes
     * reached; the IMU's is Bluepad32's defaults (offset 0, scale 16384 / 13371). */
    void set_joycon_calibration(int slot, bool left, const std::vector<FixtureRow>& rows)
    {
        std::array<std::vector<int>, 2> rest, all;
        for (const FixtureRow& row : rows) {
            if (row.slot != slot || row.r.size() < 12 || row.r[0] != 0x30)
                continue;
            const auto v = joycon_stick(row.r, left);
            for (int a = 0; a < 2; ++a) {
                all[a].push_back(v[a]);
                if (row.label == "idle")
                    rest[a].push_back(v[a]);
            }
        }
        int center[2], above[2], below[2];
        for (int a = 0; a < 2; ++a) {
            std::vector<int>& r = rest[a].empty() ? all[a] : rest[a];
            if (r.empty())
                return;
            std::nth_element(r.begin(), r.begin() + r.size() / 2, r.end());
            center[a] = r[r.size() / 2];
            above[a] = *std::max_element(all[a].begin(), all[a].end()) - center[a];
            below[a] = center[a] - *std::min_element(all[a].begin(), all[a].end());
        }
        uint8_t cal[9];
        if (left) {  // max above centre, centre, min below centre
            pack12(cal, above[0], above[1]);
            pack12(cal + 3, center[0], center[1]);
            pack12(cal + 6, below[0], below[1]);
            fake_joycon_set_spi(slot, 0x603d, cal, sizeof(cal));
        } else {  // centre, min below centre, max above centre
            pack12(cal, center[0], center[1]);
            pack12(cal + 3, below[0], below[1]);
            pack12(cal + 6, above[0], above[1]);
            fake_joycon_set_spi(slot, 0x6046, cal, sizeof(cal));
        }
        uint8_t imu[24]{};
        for (int i = 0; i < 3; ++i) {
            imu[6 + 2 * i] = 16384 & 0xFF;
            imu[7 + 2 * i] = 16384 >> 8;
            imu[18 + 2 * i] = 13371 & 0xFF;
            imu[19 + 2 * i] = 13371 >> 8;
        }
        fake_joycon_set_spi(slot, 0x6020, imu, sizeof(imu));
    }

    bool replay_joycons(const std::vector<FixtureRow>& rows)
    {
        /* Which half each slot is: a left Joy-Con fills only the left stick bytes. */
        int left_slot = -1, right_slot = -1;
        for (const FixtureRow& row : rows) {
            if (row.r.size() < 12 || row.r[0] != 0x30 || row.slot < 0 || row.slot >= CONFIG_BLUEPAD32_MAX_DEVICES)
                continue;
            const bool l = row.r[6] | row.r[7] | row.r[8], r = row.r[9] | row.r[10] | row.r[11];
            if (l && !r && left_slot < 0) left_slot = row.slot;
            if (r && !l && right_slot < 0) right_slot = row.slot;
        }
        uni_hid_parser_switch_set_pair_imu_side(true);  // the default (Adapter Options)
        uni_hid_parser_switch_set_joycon_pairing(true);
        for (int slot = 0; slot < CONFIG_BLUEPAD32_MAX_DEVICES; ++slot) {  // connected in slot order
            if (slot != left_slot && slot != right_slot)
                continue;
            const bool left = slot == left_slot;
            set_joycon_calibration(slot, left, rows);
            uni_hid_device_t* d = fake_device(slot);
            d->controller_type = left ? CONTROLLER_TYPE_SwitchJoyConLeft : CONTROLLER_TYPE_SwitchJoyConRight;
            d->vendor_id = 0x057e;
            d->product_id = left ? 0x2006 : 0x2007;
            uni_hid_parser_switch_setup(d);
            fake_joycon_run_setup(slot, left ? 1 : 2, 50);  // the emulator's device type: 1 L, 2 R
            if (!fake_device_ready(slot))
                return false;
        }
        uint32_t last_t = rows.empty() ? 0 : rows.front().t;
        for (const FixtureRow& row : rows) {
            if (row.slot != left_slot && row.slot != right_slot)
                continue;
            fake_advance_ms(row.t > last_t ? row.t - last_t : 0);
            last_t = row.t;
            uni_hid_device_t* d = fake_device(row.slot);
            s_ctx.row = &row;
            uni_hid_parser_switch_init_report(d);
            uni_hid_parser_switch_parse_input_report(d, row.r.data(), static_cast<uint16_t>(row.r.size()));
            s_ctx.row = nullptr;
        }
        return left_slot >= 0 || right_slot >= 0;
    }

} // namespace

bool bt_has_touchpad(int controller_type)
{
    return controller_type == CONTROLLER_TYPE_PS4Controller || controller_type == CONTROLLER_TYPE_PS5Controller;
}

bool bt_has_capture(int controller_type)
{
    switch (static_cast<uni_controller_type_t>(controller_type)) {
        case CONTROLLER_TYPE_SwitchProController:
        case CONTROLLER_TYPE_SwitchJoyConLeft:
        case CONTROLLER_TYPE_SwitchJoyConRight:
        case CONTROLLER_TYPE_SwitchJoyConPair:
            return true;
        default:
            return false;
    }
}

bool bt_replay(const std::vector<FixtureRow>& rows, int controller_type, uint16_t vid, uint16_t pid,
               const Gamepad& gamepad, std::vector<ReplayFrame>& out)
{
    fake_reset();
    s_ctx = Context{&gamepad, &out, nullptr};
    fake_on_controller = on_controller;

    switch (static_cast<uni_controller_type_t>(controller_type)) {
        case CONTROLLER_TYPE_SwitchJoyConLeft:
        case CONTROLLER_TYPE_SwitchJoyConRight:
        case CONTROLLER_TYPE_SwitchJoyConPair:
            return replay_joycons(rows);
        case CONTROLLER_TYPE_PS4Controller:
            break;
        default:
            return false;
    }

    uni_hid_device_t* d = fake_device(0);
    d->controller_type = static_cast<uni_controller_type_t>(controller_type);
    d->vendor_id = vid;
    d->product_id = pid;
    d->report_parser.setup = uni_hid_parser_ds4_setup;
    d->report_parser.init_report = uni_hid_parser_ds4_init_report;
    d->report_parser.parse_input_report = uni_hid_parser_ds4_parse_input_report;
    d->report_parser.setup(d);
    for (const FixtureRow& row : rows) {
        if (row.slot != 0 || row.r.empty())
            continue;
        d->report_parser.init_report(d);
        d->report_parser.parse_input_report(d, row.r.data(), static_cast<uint16_t>(row.r.size()));
        s_ctx.row = &row;
        push_frame(d);  // Bluepad32 calls the platform after the parser returns
        s_ctx.row = nullptr;
    }
    return true;
}
