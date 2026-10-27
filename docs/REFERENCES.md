# Reference projects

Well-established projects this fork checks its behaviour against: when a mode or a controller
does something that is not obvious (a battery scale, a trigger click, a calibration block), the
choice follows one of these, and the CHANGELOG entry says which. Add a project here the first
time a decision leans on it.

## Host drivers (how a PC reads the controller we emulate)

| Project | What we take from it | Used for |
|---|---|---|
| [Linux hid-playstation](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c) | DS4 / DualSense report layout, battery and charging status byte, calibration report formulas, feature reports a host reads at start | PS4 and STEAM outputs; DS4 / DualSense battery in the diagnostics report |
| [Linux hid-nintendo](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-nintendo.c) | Switch Pro subcommands and SPI reads (each stick's calibration read on its own) | Switch output, emulated SPI flash |
| [SDL HIDAPI drivers](https://github.com/libsdl-org/SDL/tree/main/src/joystick/hidapi) (`SDL_hidapi_ps4.c`, `SDL_hidapi_ps5.c`, `SDL_hidapi_switch.c`) | How games and Steam read the same reports; DualSense rumble flags (the "improved rumble" flag); `TOUCHPAD` and `MISC1` (Capture / Share / mic) as two buttons | STEAM, PS4 and Switch outputs; the touchpad press apart from Misc (2026-10-27) |
| [DS4Windows](https://github.com/Ryochan7/DS4Windows) | A second reading of the DS4 (battery scale 0-8 / 0-11, calibration over Bluetooth with report 0x05) | Compared 2026-10-22; Linux's battery scale kept (a DS4 reported level 9 off the cable) |
| [Chromium gamepad](https://chromium.googlesource.com/chromium/src/+/main/device/gamepad/) | Switch Pro wired USB init sequence | Wired Switch Pro host |

## Controller firmware (how a controller presents itself)

| Project | What we take from it | Used for |
|---|---|---|
| [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) | Per-output trigger handling: PS4 / PS3 set the L2 / R2 bits with the trigger and send 0xFF for a digital one; Switch sets ZL / ZR for any trigger value | All gamepad outputs (checked 2026-10-23) |
| [HayBox](https://github.com/JonnyHaystack/HayBox) | GameCube output: a trigger at the end of its travel is both the analog value and the digital L / R click | GameCube and Wii U adapter outputs |
| [HOJA (HandHeldLegend)](https://github.com/HandHeldLegend/HOJA-LIB-ESP32) | Switch Pro emulation with an SPI flash: the stick calibration it declares is built from its own output's centre and range, not copied from a real pad | Switch output, emulated SPI flash (2026-10-23) |
| [BlueRetro](https://github.com/darthcloud/BlueRetro) | Bluetooth controllers to retro consoles; community protocol notes (Switch 2) | Retro outputs, Switch 2 pads |
| [Bluepad32](https://github.com/ricardoquesada/bluepad32) | The Bluetooth controller stack itself (patched, `Firmware/external/patches`) | Every Bluetooth controller |

## Pico SDK fixes (backported to 2.1.0 until v1.4.1; in 2.3.1 since 2026-10-27)

| Project | What we take from it | Used for |
|---|---|---|
| [pico-sdk #2838](https://github.com/raspberrypi/pico-sdk/pull/2838) | Async context: reset `alarm_pending` when the alarm is cancelled with no timer left (issue #2836), and its regression test | Pico W / Pico 2 W Bluetooth timers (`pico_sdk_async_context_alarm_pending.diff`, 2026-10-26) |
| [pico-sdk #2127](https://github.com/raspberrypi/pico-sdk/pull/2127), [#2190](https://github.com/raspberrypi/pico-sdk/pull/2190), [#2382](https://github.com/raspberrypi/pico-sdk/pull/2382) | Alarm pool: `ta_set_timeout()` always leaves an alarm armed; `ta_wakes_up_on_or_before()` past 2^32 us; unclaim on the right timer | The same (`pico_sdk_time_adapter_alarm.diff`); with #2838 alone the SDK's test still failed on a Pico 2 W |

## Protocol notes

| Project | What we take from it | Used for |
|---|---|---|
| [dekuNukem Nintendo_Switch_Reverse_Engineering](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering) | Switch Pro / Joy-Con reports, subcommands, SPI flash map (stick calibration packing at 0x603D / 0x6046, user calibration at 0x8010) | Switch output, emulated SPI flash |
| [HandHeldLegend procon2tool](https://github.com/HandHeldLegend/procon2tool), [Nadeflore/switch2-controllers](https://github.com/Nadeflore/switch2-controllers) | Switch 2 Pro / Joy-Con 2 bring-up | Switch 2 pads (USB and BLE) |
