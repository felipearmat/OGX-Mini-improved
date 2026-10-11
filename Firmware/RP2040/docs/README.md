# Documentation index

What the adapter does, use cases, hardware, modes and support live in the [main README](../../../README.md).

This folder holds **firmware user guides**, **references** and **contributor** docs. Plans and project status (roadmap, known issues, tests, upstream tracking) are under [`docs/`](../../../docs/README.md) at the repository root.

---

## Support & contributing

| Document | Description |
|----------|-------------|
| [Support_Issue_Requirements.md](Support_Issue_Requirements.md) | **Required details** for GitHub issues. Incomplete reports may be **closed or delayed**. |
| [Web_App_Guide.md](Web_App_Guide.md) | Check a controller in the web app (buttons, sticks, touchpad, rumble, link health) and generate the **diagnostics log report** for an issue. |
| [Building_From_Source.md](Building_From_Source.md) | Clone, submodules, **required tools**, Pico SDK, and build/flash on Linux / macOS / Windows. |
| [Firmware_Architecture.md](Firmware_Architecture.md) | How the firmware is structured, runtime flow, modules, and **every file to touch** when adding host/device drivers. |
| [Adding_Supported_Controllers.md](Adding_Supported_Controllers.md) | Add a new input pad: **dedicated driver by default**, adapter-side capture, PadIn mapping, Debug UART. |
| [Tools/controller_capture](../../../Tools/controller_capture/README.md) | Legacy PC helpers — **not accepted** for mapping submissions |
| [README — Getting help and support](../../../README.md#getting-help-and-support) | Where to report issues; the original project's support policy. |
| [Firmware/common](../../common/README.md) | Code shared by the RP2040 and ESP32 firmwares, and the checks that keep them in sync. |
| [TESTING.md](../../../docs/TESTING.md) | Unit tests, documentation checks and what CI runs. |

---

## Feature guides

How to use specific output modes and related setup.

| Document | Description |
|----------|-------------|
| [SteamOS_Bazzite_Output_Mode.md](SteamOS_Bazzite_Output_Mode.md) | **STEAM** mode: DualSense USB + touchpad → HID mouse (SteamOS / Bazzite / Linux desktop). |
| [PS3_PS4_Motion_Controls.md](PS3_PS4_Motion_Controls.md) | PS3 / PS4 motion; PS4 mode is a complete DS4 on PC, and needs a licensed auth adapter on a PS4 console. |
| [Wii_Mode_Guide.md](Wii_Mode_Guide.md) | Wii (Wiimote) output — build-option only: extensions, sync, mapping. |
| [PICO2W_WII_USB_SETUP.md](PICO2W_WII_USB_SETUP.md) | Pico W / Pico 2 W USB host wiring (PIO USB) for Wii mode. |
| [GPIO_Output_Pinout_and_Mappings.md](GPIO_Output_Pinout_and_Mappings.md) | GPIO pin-outs and mappings: PS1/PS2, Dreamcast, GameCube, N64. |

---

## Reference (lists & mappings)

| Document | Description |
|----------|-------------|
| [Wired_Controllers.md](Wired_Controllers.md) | Supported **wired USB** input pads by host driver (VID/PID lists). |
| [Flydigi_APEX4_Wukong.md](Flydigi_APEX4_Wukong.md) | **Flydigi APEX 4 Black Myth: Wukong** — wired + Bluetooth modes, limitations, 2.4 GHz untested. |
| [GameSir_Cyclone2.md](GameSir_Cyclone2.md) | **GameSir Cyclone 2** — wired, 2.4 GHz dongle, and Bluetooth mode matrix. |
| [Victrix_Gambit.md](Victrix_Gambit.md) | **Victrix Gambit Tournament Controller / Gambit Prime** — wired Xbox GIP (`0E6F:0250` physically verified). Press Home after connect. |
| [Controller_Mappings.md](Controller_Mappings.md) | **PadIn** master reference: input → PadIn → every output mode. |

---

## Technical notes & changelog

| Document | Description |
|----------|-------------|
| [Firmware_Architecture.md](Firmware_Architecture.md) | Folder structure, Core0/Core1 flow, PadIn bridge, host/device checklists, external modules. |
| [ROADMAP.md](../../../docs/ROADMAP.md) | This fork's plan, in priority order. |
| [Planned_Additions.md](Planned_Additions.md) | The original creator's wish list and researched projects (Xbox wireless dongle, more retro consoles). |
| [IMPROVEMENTS.md](IMPROVEMENTS.md) | Deep technical notes: BT stability, Switch 2 / Triton, latency, rumble, unplug detection. |
| [Dreamcast_Port.md](Dreamcast_Port.md) | Dreamcast (Maple) port status / completing the port from DreamPicoPort. |
| [CHANGELOG.md](../../../CHANGELOG.md) | Version history and release notes (repo root). |
