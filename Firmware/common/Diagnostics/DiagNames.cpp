#include "Diagnostics/Diagnostics.h"

/* Name tables of the diagnostics, shared by the RP2040 firmware (report) and the hybrid's ESP32
 * (event lines it sends over I2C). */
namespace diag {

const char* company_name(uint16_t id)
{
    switch (id) {
        case 0x0002: return "Intel";
        case 0x0006: return "Microsoft";
        case 0x000A: return "Qualcomm (CSR)";
        case 0x000D: return "Texas Instruments";
        case 0x000F: return "Broadcom";
        case 0x001D: return "Qualcomm";
        case 0x0025: return "NXP";
        case 0x0030: return "STMicroelectronics";
        case 0x0046: return "MediaTek";
        case 0x004C: return "Apple";
        case 0x0059: return "Nordic Semiconductor";
        case 0x005D: return "Realtek";
        case 0x0075: return "Samsung";
        case 0x00D2: return "Dialog Semiconductor";
        case 0x012D: return "Sony";
        case 0x0131: return "Cypress / Infineon";
        case 0x0211: return "Telink";
        case 0x02FF: return "Silicon Labs";
        case 0x0553: return "Nintendo";
        default: return nullptr;
    }
}

bool is_wireless_receiver(uint16_t vid, uint16_t pid)
{
    struct Id { uint16_t vid, pid; };
    static const Id kReceivers[] = {
        {0x045e, 0x0719}, {0x045e, 0x0291}, {0x045e, 0x02a9},  // Xbox 360 wireless receivers
        {0x045e, 0x02e6}, {0x045e, 0x02fe}, {0x045e, 0x091e},  // Xbox Wireless Adapter
        {0x054c, 0x0ba0},                                      // DualShock 4 USB wireless adapter
        {0x2dc8, 0x3106}, {0x2dc8, 0x3109},                    // 8BitDo USB wireless adapters
    };
    for (const auto& r : kReceivers)
        if (r.vid == vid && r.pid == pid) return true;
    return false;
}

} // namespace diag
