# Research notes

Technical research for features that are not a matter of adding a VID/PID to a table: what was
found and what an implementation would take. Plans and their order are in
[ROADMAP.md](../../../docs/ROADMAP.md); what exists is in [IMPROVEMENTS.md](IMPROVEMENTS.md) and the
[CHANGELOG](../../../CHANGELOG.md).

---

## Researched technical projects (not simple VID/PID adds)

These need new host drivers and/or radio stacks. They are **not** a matter of adding a VID/PID to a table.

### Xbox Wireless Adapter for Windows (`045e:02e6`, `045e:02fe`)

**Devices:**

| VID:PID | Model | USB product string | Status |
|---------|-------|-------------------|--------|
| **`045e:02e6`** | 1713 | Xbox Wireless Adapter for Windows (older) | **Not supported** |
| **`045e:02fe`** | 1790 | **XBOX ACC** (newer) | **Not supported** |

These are **USB dongles** that receive **Xbox Wireless** (proprietary 2.4 GHz) from Xbox One and Xbox Series controllers. They are **not** gamepads and do **not** enumerate as XInput devices.

**Do not confuse with the Xbox 360 PC receiver** — that **is supported** today:

| VID:PID | Device | Why it works |
|---------|--------|--------------|
| **`045e:0719`** (typical) | Xbox 360 wireless **PC receiver** | Presents a standard XInput-style USB interface (`bInterfaceSubClass=0x5D`, `bInterfaceProtocol=0x81`). Handled by `tuh_xinput` / `Xbox360WHost`. |

The One/Series dongles use a **completely different USB shape** (vendor bulk, not XInput interrupt).

#### What the dongle looks like on USB

From hardware dumps (e.g. [ControllersInfo adapter descriptor](https://github.com/DJm00n/ControllersInfo/blob/master/xboxone/DescriptorDump_Adapter%20(Xbox%20Wireless%20Adapter%20for%20Windows).txt)):

- **Device class:** vendor-specific (`0xFF/0xFF/0xFF`), product name **"XBOX ACC"**
- **Endpoints:** **bulk** IN/OUT (512-byte packets on USB 2.0), not the interrupt endpoints used by **wired** Xbox One pads
- **Inside the stick:** a **Mediatek MT76** Wi‑Fi chipset that must run **dongle firmware** before it can talk to controllers

OGX-Mini’s existing **wired Xbox One GIP** path (`tuh_xinput`, subclass `0x47` / protocol `0xD0`, `XboxOneHost`) parses GIP **after** the controller is already connected over USB. The wireless adapter never exposes that interface — the host must drive the **dongle radio** first.

#### Why this is not a simple port

1. **Firmware upload** — Linux [xone](https://github.com/medusalix/xone) / [xow](https://github.com/medusalix/xow) load **`xow_dongle.bin`** / variant blobs (e.g. **`xone_dongle_02fe.bin`**) into the MT76 chip over USB before the device is useful. That blob must be shipped in flash and the load sequence reimplemented on RP2040.

2. **Wireless stack, not HID** — The driver brings up **MT76** (channels, pairing scan, client join/leave, optional encryption), wraps **GIP** payloads in **802.11-style data frames**, and sends them on bulk OUT queues. Inbound bulk IN carries WLAN frames that must be parsed to extract GIP input. This is the bulk of [xone `transport/dongle.c`](https://github.com/medusalix/xone/blob/master/transport/dongle.c) + [`transport/mt76.c`](https://github.com/medusalix/xone/blob/master/transport/mt76.c) — thousands of lines, not a report-descriptor tweak.

3. **Resource cost** — xone uses many bulk URBs, WLAN buffers up to tens of KB per packet, and ongoing radio work. RP2040 flash/RAM and Core1 USB host timing are tight compared to a PC kernel driver.

4. **Pairing model** — Controllers must be **paired to the dongle** (Sync on the pad while the dongle is in pairing mode). Pads previously used over **USB** or **Bluetooth** will not auto-attach until re-paired to the dongle — same as on Windows with xone.

5. **Reuse is partial only** — Once a wireless client is connected and GIP input arrives, decoding could **reuse** existing GIP constants and mapping from `Descriptors/XboxOne.h` / `XboxOneHost.cpp`. Everything **before** that (USB dongle + radio + framing) is new work — comparable in scope to adding Switch 2 bulk bring-up, but **larger** because of firmware + Wi‑Fi.

**Rough implementation phases (if pursued):**

1. New TinyUSB **vendor bulk** class driver; claim `045e:02e6` / `045e:02fe`; embed firmware; port MT76 init from xone.  
2. Bulk I/O loops, pairing, client add/remove (mirror `Xbox360W` connect callbacks in `HostManager`).  
3. Bridge decoded GIP `0x20` / `0x07` reports into `XboxOneHost` (or a shared GIP parser).  
4. Rumble/LED over wireless GIP OUT path.

**Practical alternatives today:**

- **Xbox 360 wireless receiver** + 360 pads — already supported (`Xbox360WHost`).  
- **Xbox One / Series controller** — **Bluetooth** on Pico W / Pico 2 W (Bluepad32), or **wired USB** (`XboxOneHost`).  

**References:** [medusalix/xone](https://github.com/medusalix/xone), [medusalix/xow](https://github.com/medusalix/xow), [SDL discussion of 02fe vs XInput PID](https://github.com/libsdl-org/SDL/pull/8683), [ControllersInfo dongle descriptor dump](https://github.com/DJm00n/ControllersInfo/tree/master/xboxone).

**Files that would be touched (when implemented):** new `tuh_xbox_dongle` (or similar) under `USBHost/HostDriver/XInput/`, `HostManager.h`, `tuh_callbacks.cpp`, `tusb_config.h`; possible shared GIP layer with `XboxOne.cpp`.
