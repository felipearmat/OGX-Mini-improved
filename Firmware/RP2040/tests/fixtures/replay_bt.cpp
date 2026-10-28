// The Bluetooth half of the fixture replay (fixture_replay.h).
extern "C" {
#include "fake_bt.h"
#include "parser/uni_hid_parser_ds4.h"
}
#include "Bluepad32/BtPadIn.h"
#include "fixture_replay.h"

bool bt_replay(const std::vector<FixtureRow>& rows, int controller_type, uint16_t vid, uint16_t pid,
               const Gamepad& gamepad, std::vector<ReplayFrame>& out)
{
    fake_reset();
    uni_hid_device_t* d = fake_device(0);
    d->controller_type = static_cast<uni_controller_type_t>(controller_type);
    d->vendor_id = vid;
    d->product_id = pid;
    switch (d->controller_type) {
        case CONTROLLER_TYPE_PS4Controller:
            d->report_parser.setup = uni_hid_parser_ds4_setup;
            d->report_parser.init_report = uni_hid_parser_ds4_init_report;
            d->report_parser.parse_input_report = uni_hid_parser_ds4_parse_input_report;
            break;
        default:
            return false;
    }
    d->report_parser.setup(d);

    MotionImu::WiiPseudoGyroState wii{};
    for (const FixtureRow& row : rows) {
        if (row.slot != 0 || row.r.empty())
            continue;
        d->report_parser.init_report(d);
        d->report_parser.parse_input_report(d, row.r.data(), static_cast<uint16_t>(row.r.size()));
        ReplayFrame f{row.t, &row.label, bt_pad::pad_in_from(d, &d->controller.gamepad, gamepad, wii, row.t),
                      bt_pad::ps5_core_from(d, &d->controller.gamepad)};
        out.push_back(f);
    }
    return true;
}
