# SteamOS / Bazzite output mode

Use this mode on **SteamOS**, **Bazzite**, or other **Linux desktops** (including **Steam Deck** in desktop mode) when you want the host to see a **PS5 DualSense** gamepad **and** use the **touchpad as a USB mouse** (cursor movement + tap to click). Games and Steam Input see a normal DualSense; the desktop gets a separate **HID mouse** for navigating outside games.

## Select mode

| Method | Combo / option |
|--------|----------------|
| **Button combo** | Hold **Start + Left Bumper + D-pad Up** for ~3 seconds (saved to flash; device resets) |
| **Web app** | Output mode **SteamOS / Bazzite** |
| **Fixed build** | `-DOGXM_FIXED_DRIVER=STEAM` (also in `./scripts/build.sh` / `build.ps1` fixed-mode menu) |

## DualSense USB emulation

The adapter presents **two USB interfaces** to the PC:

| Interface | Identity | Purpose |
|-----------|----------|---------|
| **Gamepad** | Sony **DualSense** `054c:0ce6`, 64-byte HID input report | Buttons, sticks, triggers, PS button — for Steam / Proton / desktop gamepad APIs |
| **Mouse** | Standard **relative HID mouse** (separate interface) | Cursor from the **DualSense or DS4 touchpad** |

**Input → USB behavior:**

| Input controller | Gamepad report | Touchpad / mouse |
|------------------|----------------|------------------|
| **DualSense (PS5)** — Bluetooth or wired USB | **Passthrough** of the real DualSense report (sticks/triggers from host when wired) | Touchpad finger position → **relative mouse** movement; touchpad click (`TP`) → **left click** |
| **Other pads** (Xbox, DS4, Switch Pro, Joy-Cons, etc.) | **Synthesized** DualSense report — face buttons, shoulders, triggers, sticks, D-pad, **Share/Options**, **PS**; in OGX-Mini-improved also **motion** (real DualSense units + calibration), **touch points and click** (DS4), **battery**, and the host's **lightbar / rumble** | **DS4:** touchpad → **relative mouse**, click → **left click**. Pads without a touchpad: gamepad only |

Face-button mapping to DualSense: **A→Cross**, **B→Circle**, **X→Square**, **Y→Triangle**; **LB/RB→L1/R1**; **Back/Start→Share/Options**; **SYS→PS**. Full table: [Controller_Mappings.md — STEAM mode](Controller_Mappings.md#steamos--bazzite-steam-mode).

After switching to STEAM mode, **unplug and replug** the adapter’s USB cable to the PC so Linux re-enumerates both interfaces. Verify with `lsusb -d 054c:0ce6` (two interfaces) and `evtest` on the mouse device.

## Touchpad → mouse (DualSense or DS4 input)

When the **input** pad is a **DualSense** (Bluetooth on Pico W / Pico 2 W, or wired into the adapter’s USB host port), or a **DS4** over Bluetooth (OGX-Mini-improved):

- **Drag** on the touchpad → **relative mouse** movement on the PC (second USB interface).
- **Tap / click** the touchpad → **left mouse button**.
- The touchpad data is also embedded in the DualSense gamepad report (offset compatible with Linux `hid-playstation`).

Controllers **without** a touchpad still work as a DualSense gamepad; they do **not** drive the mouse interface.

On Linux the kernel `hid-playstation` driver also claims the mouse interface (same VID/PID) and leaves it without a driver ("Duplicate device found for MAC address"). The cursor still works there: the kernel exposes the DualSense touchpad as a pointer itself, and Steam's desktop layout drives it too.

## Other notes

- **On-screen keyboard:** Steam handles this — **Steam + X** on the controller (**PS + Square** on DualSense), same as Steam Deck desktop.
- **Input sources:** Most supported **USB** and **Bluetooth** controllers work as gamepad input; **touchpad → mouse** requires a **DualSense** or a **DS4**.

Fixed-build example (Pico 2 W):  
`cmake -DOGXM_BOARD=PI_PICO2W -DCMAKE_BUILD_TYPE=Release -DOGXM_FIXED_DRIVER=STEAM ..` then `ninja`.

Technical detail: [IMPROVEMENTS.md — STEAM mode](IMPROVEMENTS.md#steam-mode--steamos--bazzite-linux-desktop).
