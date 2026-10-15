# OGX-Mini-improved

**Open-source adapter firmware that lets your controllers play on systems they were never made for.**
Bluetooth or wired controllers in; Xbox 360, Switch, PC / Steam, PS3, Wii and retro consoles out —
on cheap hardware: a Raspberry Pi Pico 2 W, or an RP2040 board paired with an ESP32.

[Download](https://github.com/felipearmat/OGX-Mini-improved/releases) ·
[Web app](https://felipearmat.github.io/OGX-Mini-improved-WebApp/) ·
[Use cases](#use-cases) · [Which hardware](#which-hardware) · [Output modes](#output-modes) ·
[Controllers](#controllers) · [Roadmap](docs/ROADMAP.md) · [Changelog](CHANGELOG.md)

## TL;DR

Plug the adapter into a console or PC, pair a controller, play. The adapter shows up as the
controller that system expects — an Xbox 360 pad, a Switch Pro Controller, a DualShock 4, a
DualSense for Steam, a Wiimote — and translates buttons, sticks, triggers, rumble and motion as
you play.

- **Bluetooth → USB:** DualSense, DualShock 4 / 3, Xbox One / Series, Switch Pro / Switch 2 Pro,
  Joy-Con (a pair works as one controller), Wii Remote, Steam Controller and more.
- **Wired → USB:** Xbox 360 / One / Series, DualShock, Switch Pro, arcade sticks and 2.4 GHz
  dongles, on boards with a USB-A port.
- **To retro consoles:** PS1 / PS2, GameCube, N64 and Dreamcast through the console's controller
  cable.
- **Motion and rumble** carried across where the target supports them (Switch, PS3 / PS4, Steam).
- **Settings without installing anything:** button mapping, stick and trigger curves, profiles
  and adapter options in a browser web app, over USB or Bluetooth.
- **Wakes your PC** from sleep with a button press.

**Get started:** pick a board ([which hardware](#which-hardware)) → hold BOOTSEL, plug it in and
copy the `.uf2` from [Releases](https://github.com/felipearmat/OGX-Mini-improved/releases) → put
your controller in pairing mode → pick the mode with a [button combo](#output-modes).

**Not supported:** native PS4 / PS5 and Xbox One / Series consoles ([why](#limitations)).

---

## Contents

- [How it works](#how-it-works)
- [Use cases](#use-cases)
- [Which hardware](#which-hardware)
- [Getting started](#getting-started)
- [Output modes](#output-modes)
- [Controllers](#controllers)
- [Web app and adapter options](#web-app-and-adapter-options)
- [Mouse + Keyboard mode](#mouse--keyboard-mode)
- [Diagnostics and log report](#diagnostics-and-log-report)
- [Waking the PC from the controller](#waking-the-pc-from-the-controller)
- [Limitations](#limitations)
- [Roadmap](#roadmap)
- [Building from source](#building-from-source)
- [Getting help and support](#getting-help-and-support)
- [Documentation](#documentation)
- [Credits and licenses](#credits-and-licenses)

## How it works

```
  controller                         adapter                              console / PC
 ─────────────                ──────────────────────────               ─────────────────
  Bluetooth  ──┐               reads the controller,                   USB: Xbox 360, Switch,
  wired USB  ──┼──────────▶   maps it to a common pad,   ──────────▶   PS3, PC / Steam, Wii U,
  2.4 GHz    ──┘               then speaks the output mode             Original Xbox, ...
  dongle                       you picked (button combo)               Bluetooth: Wii (Wiimote)
                                                                       cable: PS1/2, GC, N64, DC
```

The adapter is a small microcontroller board (RP2040 / RP2350). Bluetooth controllers connect to
its radio (Pico W / Pico 2 W, or an ESP32 next to an RP2040); wired controllers and 2.4 GHz dongles
plug into a USB-A port on boards that have one. Whatever comes in, the adapter turns it into one
internal pad state, then presents that state as the controller the output mode emulates. The mode
is stored in flash and kept across power cycles.

This is a fork of [MegaCadeDev/OGX-Mini-2026](https://github.com/MegaCadeDev/OGX-Mini-2026)
(itself based on [wiredopposite's OGX-Mini](https://github.com/wiredopposite/OGX-Mini)),
developed and tested on a **Raspberry Pi Pico 2 W** as a Bluetooth → USB adapter for PC and
Nintendo Switch. What the fork adds and fixes is listed in the [CHANGELOG](CHANGELOG.md).

## Use cases

| I want to… | Mode | Notes |
|---|---|---|
| Use my PlayStation / Xbox / Switch controller on a **Switch or Switch Lite** | Switch Pro | Motion and rumble included. Docked, or in handheld mode through a USB-C OTG adapter with charging pass-through. |
| Use a modern wireless controller on an **Xbox 360** | XInput | Authenticates like an original pad; one adapter per player. |
| Get full **DualSense / DualShock 4** features on **Steam, SteamOS or Bazzite** | STEAM or PS4 | Touchpad (also as mouse), motion, lightbar, rumble, battery. |
| Use **any controller as a DualShock 4 on a PC** | PS4 | A complete DS4: touchpad, motion, lightbar and rumble from the host. |
| **Wake my gaming PC** from the couch | any | One-time setup on Linux / Windows: [waking the PC](#waking-the-pc-from-the-controller). |
| Play **Wii and Wii U** games | Wii | Wiimote with Nunchuk or Classic Controller over Bluetooth; Wii U games that take the Classic Controller work. The controller is wired (USB) in this mode for now. |
| Play on a **PS3** with any controller | PS3 | Motion passed through for Sixaxis games. |
| Use a modern controller on a **PS1 / PS2, GameCube, N64 or Dreamcast** | GPIO modes | Wired to a controller extension cable. |
| Use an **Original Xbox** | Original Xbox | Also Steel Battalion and the DVD remote. |
| Drive a PC, BIOS, smart TV or console menu with the controller as a **keyboard and mouse** | Mouse + Keyboard | Remappable; can power on PCs that allow USB-keyboard wake. |

## Which hardware

| Use | Recommended hardware | Why |
|---|---|---|
| Bluetooth controllers on PC, Steam, Switch, Xbox 360, PS3 (most people) | Raspberry Pi Pico 2 W (or Pico W) | One board, Bluetooth built in, nothing to wire |
| Wired controllers or 2.4 GHz dongles | A board with a USB-A port: Waveshare RP2350-USB-A, Adafruit Feather RP2040 USB Host | Plug the controller straight in |
| Bluetooth **and** wired on one adapter | Pico 2 W + a USB-A socket on GP0 / GP1 | Built-in Bluetooth plus a second USB port |
| Longer Bluetooth range, several Bluetooth players *(experimental)* | RP2040 or Pi Pico 2 / Pico 2 W + an ESP32 with an external antenna (ESP32-WROOM-32U) | External antenna, higher transmit power. Builds today; not yet tested on hardware ([roadmap](docs/ROADMAP.md#6-rp2040--esp32-hybrid)). |
| Wii / Wii U | Pico 2 W + a USB-A socket (wired USB controller) | The radio talks to the console as a Wiimote |
| PS1 / PS2, GameCube, N64, Dreamcast | Pico 2 W + a controller extension cable | Bluetooth controller in, console cable out |
| Original Xbox, four players | 4-channel RP2040-Zero board | One USB device per console port |
| Smallest wired-only adapter | RP2040-Zero / RP2350-Zero | Tiny, USB-C |

Boards, pinouts and wiring diagrams: [hardware/](hardware/README.md) and
[GPIO pinouts](Firmware/RP2040/docs/GPIO_Output_Pinout_and_Mappings.md). Every `OGXM_BOARD` value
is listed under [Building from source](#building-from-source).

## Getting started

1. **Download** the `.uf2` for your board from
   [Releases](https://github.com/felipearmat/OGX-Mini-improved/releases).
2. **Flash:** hold the board's **BOOTSEL** button while plugging it in, then copy the file to the
   drive that appears. The board restarts on its own.
3. **Pair:** put the controller in pairing mode (or plug it in, on boards with USB-A). The LED
   shows what the adapter is doing:
   - at boot it **blinks the mode number** (see [Output modes](#output-modes));
   - then a **fast blink** means it is searching for new Bluetooth controllers, a **slow blink** a
     reduced search (a slot open for over a minute, e.g. a lone Joy-Con waiting for its other
     half), and **solid** means no search (every slot in use). Searching takes radio time from
     connected controllers, so the adapter stops when it has all it can use; a controller it
     already knows reconnects either way.
4. **Pick the mode** with a button combo (below) or in the web app. The adapter restarts in the
   new mode and remembers it.

A DualShock 4 / DualSense lightbar shows the mode colour until the host sets its own colour. An
optional external WS2812B LED on the Pico 2 W (GP6; also on the Pico 2 / Pico 2 W of the ESP32
hybrid) repeats the blink code and states in the
mode colour: [wiring](hardware/README.md#pico-2-w-external-rgb-status-led).

## Output modes

Hold the combo for about **3 seconds**. On a DualShock 4 / DualSense, Start = Options and
LB = L1; on Joy-Cons, Start = +. The default mode (first boot) is Original Xbox.

<!-- BEGIN GENERATED: mode-table (edit .github/scripts/gen_docs_tables.py, not this table) -->
| Mode | Combo (hold ~3 s) | Host sees | LED blinks | Lightbar | RP2040 + ESP32 | For |
|---|---|---|---|---|---|---|
| XInput | Start + Up | Xbox 360 controller | 1 | green | yes | PC, Steam, Xbox 360 console (authenticates like an original pad) |
| Switch Pro | Start + Down | Switch Pro Controller | 2 | red | yes | Switch / Switch 2 / Lite, PC; motion and HD rumble |
| DInput | Start + RB + Left | generic gamepad | 3 | blue | yes | PC and other hosts that take a generic gamepad |
| PS4 | Start + LB + Left | DualShock 4 | 4 | light blue | yes | PC (complete DS4: touchpad, motion, lightbar); a PS4 console needs a licensed auth adapter |
| STEAM | Start + LB + Up | DualSense + mouse | 5 | purple | yes | Steam / SteamOS / Bazzite; touchpad as mouse |
| Original Xbox | Start + Right | Xbox controller (Duke) | 6 | yellow | yes | Original Xbox console |
| Original Xbox: Steel Battalion | Start + RB + Right | Steel Battalion controller | 6 | yellow | — | Original Xbox console |
| Original Xbox: DVD remote | Start + LB + Right | Xbox DVD remote | 6 | yellow | — | Original Xbox console (only in builds with the remote ROM) |
| PS3 | Start + Left | DualShock 3 | 7 | dark blue | yes | PS3 console (motion for Sixaxis games), PC |
| Mouse + Keyboard | Start + RB + Up | keyboard + mouse + media keys | 8 | orange | yes | PCs, BIOS / UEFI, smart TVs, consoles that take a mouse and keyboard |
| PS Classic | Start + A | PlayStation Classic controller | 7 | white | yes | PlayStation Classic console |
| Wii U (GameCube adapter) | Start + LB + Down | Wii U GameCube adapter | 7 | white | yes | Wii U, in Super Smash Bros. only; the one mode tested with several controllers |
| Wii | fixed build: `-DOGXM_FIXED_DRIVER=WII` | Wiimote over Bluetooth | 7 | white | — | Wii / Wii U; Pico W / Pico 2 W with a wired USB controller |
| PS1 / PS2 | Start + B | PlayStation controller (wired) | 7 | white | — | PS1 / PS2 through the controller cable; Pico W / Pico 2 W |
| GameCube | fixed build: `-DOGXM_FIXED_DRIVER=GAMECUBE` | GameCube controller (wired) | 7 | white | — | GameCube / Wii GameCube ports; Pico W / Pico 2 W |
| Dreamcast | Start + Y | Dreamcast controller (wired) | 7 | white | — | Dreamcast through the controller cable; Pico W / Pico 2 W |
| N64 | fixed build: `-DOGXM_FIXED_DRIVER=N64` | N64 controller (wired) | 7 | white | — | N64 through the controller cable; Pico W / Pico 2 W |
| Web App | Start + LB + RB | web app device | 7 | white | yes | Settings and diagnostics in the browser, over USB |
<!-- END GENERATED: mode-table -->

- **Other combos:** **Start + Select** for 3 s disconnects the controller (Options + Share on a
  DS4, + and − on Joy-Cons; L3 + R3 on an OUYA controller); Joy-Cons are asked to sleep and the
  adapter restarts in the same mode. **Start + L3** for 3 s stops the search for new controllers
  (one rumble, no restart); it starts again when a controller goes away.
- **A single Joy-Con** held sideways (no D-pad): the stick pushed to the edge counts as the D-pad
  in combos; Start = Capture (left Joy-Con) or + (right), LB / RB = SL / SR.
- **Combos can be turned off** per mode in the web app (adapter option), so a mode can't be
  entered by accident.
- **Wii, GameCube and N64 are build-only modes** (no combo): build with
  `-DOGXM_FIXED_DRIVER=WII`, `GAMECUBE` or `N64`, for example
  `cmake -DOGXM_BOARD=PI_PICO2W -DCMAKE_BUILD_TYPE=Release -DOGXM_FIXED_DRIVER=GAMECUBE ..`, then
  flash `OGX-Mini-*-GAMECUBE.uf2` (`PI_PICOW` for a Pico W). Any mode can be fixed this way
  (`OGXM_FIXED_DRIVER_ALLOW_COMBOS=ON` keeps the combos).
- **GPIO modes** (PS1 / PS2, GameCube, N64, Dreamcast) are meant for a **Pico W / Pico 2 W** with a
  **Bluetooth** controller, wired to the console's controller cable; a USB controller on the
  board's USB-A port is not supported in these modes.

Mode guides: [Steam / SteamOS / Bazzite](Firmware/RP2040/docs/SteamOS_Bazzite_Output_Mode.md) ·
[PS3 / PS4 motion](Firmware/RP2040/docs/PS3_PS4_Motion_Controls.md) ·
[Wii](Firmware/RP2040/docs/Wii_Mode_Guide.md) ·
[GPIO pinouts and mappings](Firmware/RP2040/docs/GPIO_Output_Pinout_and_Mappings.md) ·
[Dreamcast](Firmware/RP2040/docs/Dreamcast_Port.md) ·
[Button mappings per mode](Firmware/RP2040/docs/Controller_Mappings.md).

## Controllers

**First-party controllers are the focus** (Microsoft, Sony, Nintendo); other controllers often
work through the same Bluetooth and USB code but are best-effort.

### Bluetooth (Pico W / Pico 2 W / RP2354)

- **Xbox** Series, One and Elite 2; **SCUF Instinct** (Bluetooth LE, same path as Xbox Series)
- **DualShock 3**, **DualShock 4**, **DualSense**
- **Switch Pro**, **Joy-Con** (a pair merges into one controller), **Switch 2 Pro**, **Joy-Con 2**
- **Wii U Pro**, **Wii Remote** (with Nunchuk, Classic Controller / gamepad and GameCube extensions)
- **GameSir Cyclone 2**, **GameSir G7 Pro**, **Flydigi APEX 4 (Black Myth: Wukong)**
- Best-effort: original Steam Controller, **Steam Controller 2026 (Triton)**, Stadia, 8BitDo
  Ultimate / Pro 2 / SN30 Pro (use their Switch or Android mode), and the wider
  [Bluepad32 list](https://bluepad32.readthedocs.io/en/latest/supported_gamepads/)

Pairing notes:

- **DualShock 3:** it doesn't pair like other controllers. Plug it into the adapter's **USB-A**
  port once: the adapter writes its own Bluetooth address into it. Unplug, press **PS**.
- **Switch 2 Pro / Joy-Con 2:** put the controller in pairing (SYNC) mode and let the adapter find
  it — don't pair it with your PC or phone first. Joy-Con 2: pair the **left** first, then the
  **right**; both become player 1. Home works as Guide / PS.
- **Steam Controller 2026:** hold **RB + B + Steam** to pair; remove it from your PC or phone first
  (only one host can own it). View → Start, Menu → Select, Steam → Guide.
- **DualShock 4 and other Bluetooth Classic controllers on a Pico W / Pico 2 W:** the radio can't
  keep a Classic link and advertise the web app over Bluetooth LE at the same time, so the web app
  isn't visible over Bluetooth while such a controller is connected. Disconnect it, or plug the
  adapter in and use **Connect via USB**. DualSense and Xbox Series (Bluetooth LE) are unaffected.
- **GameSir G7 Pro over USB:** press Home once after plugging it in.

### Wired USB (boards with a USB-A port)

Original Xbox (Duke, S), Xbox 360 / One / Series / Elite, DualShock 3 / 4, DualSense, Switch Pro,
Switch 2 Pro and Joy-Con 2, PS Classic, generic DInput / HID (mappings may need editing in the web
app), arcade sticks such as the Razer Atrox, Victrix Gambit (press Home once after connecting),
Razer Wolverine V3 TE, GameSir Cyclone 2 / G7 Pro, Flydigi Vader 5 Pro and APEX 4. Full list by
driver: [Wired controllers](Firmware/RP2040/docs/Wired_Controllers.md).

### Wireless USB receivers

Microsoft Xbox 360 PC receiver, Flydigi Vader 5 Pro 2.4 GHz dongle, GameSir Cyclone 2 dongle.
The Xbox One / Series wireless dongle is not supported (it is a Wi-Fi radio, not a gamepad; see
[Planned additions](Firmware/RP2040/docs/Planned_Additions.md#xbox-wireless-adapter-for-windows-045e02e6-045e02fe)).

Controller-specific pages: [GameSir Cyclone 2](Firmware/RP2040/docs/GameSir_Cyclone2.md) ·
[GameSir G7 Pro](Firmware/RP2040/docs/GameSir_G7Pro.md) ·
[Flydigi APEX 4](Firmware/RP2040/docs/Flydigi_APEX4_Wukong.md) ·
[Victrix Gambit](Firmware/RP2040/docs/Victrix_Gambit.md). Controllers that change their USB IDs by
mode or batch may not work and are not a focus for new support.

## Web app and adapter options

The [web app](https://felipearmat.github.io/OGX-Mini-improved-WebApp/) runs in a Chromium-based
browser (Chrome, Edge, Brave) and talks to the adapter over USB or Bluetooth — nothing to
install. It edits button mappings, stick deadzones and curves, trigger thresholds and profiles,
the Mouse + Keyboard layout, the adapter options below, and shows live input and diagnostics.

- **Over USB:** switch the adapter to **Web App** mode (Start + LB + RB), plug it into the PC, press
  *Connect via USB*.
- **Over Bluetooth:** press *Connect via Bluetooth*; no mode change needed (see the Bluetooth
  Classic note under [Controllers](#bluetooth-pico-w--pico-2-w--rp2354)).

Guide: [Web app guide](Firmware/RP2040/docs/Web_App_Guide.md). Source and local use:
[OGX-Mini-improved-WebApp](https://github.com/felipearmat/OGX-Mini-improved-WebApp) (clone, run
`./serve.sh`, open http://localhost:8000).

**Adapter options** (web app; the CMake option sets the firmware default):

| Option | CMake default |
|---|---|
| Turn off controllers before mode change | `OGXM_DISCONNECT_PADS_ON_MODE_CHANGE` = ON |
| Joy-Con pair: motion from the right / left Joy-Con | `OGXM_JOYCON_PAIR_IMU_SIDE` = RIGHT |
| Joy-Con pair orientation | `OGXM_JOYCON_PAIR_ORIENTATION` = VERTICAL |
| Single Joy-Con orientation | `OGXM_JOYCON_SOLO_ORIENTATION` = HORIZONTAL |
| Use a MAC address per controller (PS4 / STEAM) | `OGXM_MAC_PER_CONTROLLER` = OFF |
| Legacy PS4 motion scale (Brook auth adapters) | `OGXM_PS4_LEGACY_MOTION_SCALE` = OFF |
| No search for new controllers once one is connected (both search times 0) | `OGXM_SINGLE_CONTROLLER` = OFF |
| Joy-Con pair rumble: per side (as SDL / Steam) or both Joy-Cons | `OGXM_JOYCON_PAIR_RUMBLE` = PER_SIDE |
| Full search for new controllers (seconds, 0-600) | `OGXM_FULL_SEARCH_S` = 60 |
| Reduced search after it (seconds, 0-600, or no limit) | `OGXM_REDUCED_SEARCH_S` = NO_LIMIT |
| Output modes whose button combo is off (bit mask by mode number) | `OGXM_COMBO_DISABLED_MODES` = 0 (all on) |
| Turn a Bluetooth controller off after N minutes without input (0 = never) | `OGXM_IDLE_OFF_MINUTES` = 15 |

The search times let several adapters sit next to each other without stealing each other's
controllers.

## Mouse + Keyboard mode

The controller drives a USB keyboard, mouse and media keys — for hosts without gamepad navigation
(PCs without Steam, smart TVs, BIOS menus, consoles that take a mouse and keyboard). Three USB HID
interfaces, each a boot-protocol device where it can be (keyboard, mouse with wheel and horizontal
scroll, media keys), so BIOS menus, smart TVs and consoles accept it. Only the first controller
drives it. Default layout (Steam's desktop layout where it has one):

| Input | Sends |
|---|---|
| Right stick | Mouse pointer (acceleration curve) |
| Left stick | Scroll wheel (vertical and horizontal) |
| D-pad | Arrow keys |
| A / B / X / Y | Enter / Esc / Backspace / Space |
| RT / LT | Left / right click |
| RB / LB | Tab / Shift + Tab |
| L3 / R3 | Ctrl / Shift (held) |
| Start / Select | Menu key / Windows (Super) key |
| Home / Guide | Media "Home" |
| DS4 / DualSense touchpad | Pointer (click = left click) |

In the web app (**Mouse + Keyboard Mode** panel) each input can send any key (with Ctrl / Shift /
Alt / Win held), a mouse button or a media key (home, back, volume, playback, search); each stick
can be pointer, scroll, arrow keys or WASD; pointer / scroll speed, deadzone and acceleration are
adjustable. Changes apply right away, without a restart. The mode combos keep working in this
mode.

**Control the whole PC from the controller:** in this mode the controller can wake the PC from
sleep (when the PC allows wake-up by USB), navigate the BIOS / UEFI setup (map a button to Del /
F2 in the web app) and power the PC on (when the BIOS has "power on by USB keyboard" and keeps USB
powered while off). Wake-up and BIOS navigation verified on an ASUS ROG Ally X.

## Diagnostics and log report

To report lag, stutter or disconnects without a serial adapter:

1. Play in your usual mode for a few minutes with the problem happening.
2. Without unplugging the adapter, switch it to **Web App** mode with the combo, open the
   [web app](https://felipearmat.github.io/OGX-Mini-improved-WebApp/) and connect over USB.
3. Keep the controller connected and move the sticks for 10 seconds, then press
   **Generate log report** in the *Diagnostics* panel and send the downloaded `.json` file.

The report has, per controller: what it is (IDs, Bluetooth chip vendor and version, firmware
strings), how it is connected (Bluetooth Classic or LE and its connection interval, signal, radio
channels in use, wired or a known 2.4 GHz receiver, USB speed) and how its input arrives (reports
per second, late reports, largest gap, reports lost by the controller's own counter on DS4 /
DualSense). It also has a summary of the session before the switch to Web App mode and the last
events (connects, disconnects with their reason, mode changes). It holds no full Bluetooth address
(only the manufacturer part) and no personal data.

The events and the session summary survive the switch to Web App mode (the memory survives that
restart, not unplugging the adapter). Turning the controller off (or Start + Select) before
unplugging stores the session summary in flash, so it is still in a later report, together with
where the firmware stopped if it ever crashed (the adapter restarts by itself after a crash instead
of freezing). Full guide, including what each Diagnostics column means:
[Web app guide](Firmware/RP2040/docs/Web_App_Guide.md).

## Waking the PC from the controller

The adapter can wake a sleeping PC over USB, but only if the PC allows that device to wake it.
Linux allows it by default only for keyboards, so out of the box only **Mouse + Keyboard** mode
wakes a Linux PC (Bazzite, SteamOS, ...); the gamepad modes need the one-time setup below.

What wakes the PC, per output mode (USB IDs as the PC sees them):

| Mode | USB ID | Wakes the PC with |
|---|---|---|
| Mouse + Keyboard | `1209:0001` | any key, click or pointer movement |
| Switch Pro | `057e:2009` | a press: any button, the D-pad or a trigger past half travel |
| DInput | `2563:0575` | a press (as above) |
| Wii U | `057e:0337` | a press (as above) |
| PS Classic | `054c:0cda` | a press (as above) |
| XInput | `045e:028e` | a press (as above); an Xbox 360 console still wakes with Guide |
| STEAM | `054c:0ce6` (or the connected DualSense's ID) | a press (as above) |
| PS3 | `054c:0268` | PS (Home) button, or Start held 3 s |
| PS4 | `054c:05c4` | PS (Home) button, or Start held 3 s |

Sticks never wake the PC (drift or a bump in the bag would), and each sleep gets one wake request.
Verified on an ASUS ROG Ally X (Bazzite, `s2idle`) with Joy-Cons in Switch Pro mode. If the
controller fell asleep, the first press only reconnects it; press again a second or two later.

**Linux (Bazzite, SteamOS, other distros)**

1. With the adapter in the mode you use, find its ID: `lsusb` (for example
   `057e:2009 Nintendo Co., Ltd Pro Controller` in Switch Pro mode).
2. Allow it to wake the PC (until the next reboot or replug); use your mode's ID:

   ```sh
   for d in /sys/bus/usb/devices/*; do
     [ "$(cat $d/idVendor 2>/dev/null)" = "057e" ] && [ "$(cat $d/idProduct 2>/dev/null)" = "2009" ] && \
     echo enabled | sudo tee $d/power/wakeup && echo "$d: wakeup enabled"
   done
   ```

3. Test: `systemctl suspend`, then press a button on the controller.
4. Make it permanent with the rule in [Tools/linux](Tools/linux/99-ogx-mini-wakeup.rules). It covers
   every gamepad mode and also enables wakeup on the hubs and USB controller above the adapter (a
   wake request behind a hub with wakeup disabled never reaches the PC). `/etc` is writable on
   Bazzite too:

   ```sh
   sudo cp Tools/linux/99-ogx-mini-wakeup.rules /etc/udev/rules.d/
   sudo udevadm control --reload && sudo udevadm trigger -s usb -c add
   ```

   The rule also applies to an original controller with the same ID.

If it still does not wake, the PC's USB controller itself may not be allowed to wake the system:
`grep -i xhc /proc/acpi/wakeup`. If its line says `*disabled`, enable it with
`echo XHC0 | sudo tee /proc/acpi/wakeup` (use the exact name from that line; the command toggles,
so run it once).

**Windows:** Device Manager → the adapter's entry (under *Human Interface Devices*, or *Keyboards* /
*Mice* in Mouse + Keyboard mode) → *Properties* → *Power Management* → tick *Allow this device to
wake the computer*.

**Hibernation** is a power-off from the USB point of view: the controller cannot wake a hibernated
PC (only from suspend), and on most PCs the adapter itself loses power. **Powering on from off**
works only if the BIOS / UEFI has an option such as *Power on by USB keyboard* and keeps USB powered
while the PC is off, so in practice only in Mouse + Keyboard mode.

## Limitations

- **PS4 / PS5 and Xbox One / Series consoles** only accept controllers with a licensed security
  chip. On a PS4, PS4 mode is dropped after about 8 minutes unless a **licensed authentication
  adapter** sits between the adapter and the console (tested: Brook Wingman XE 2, on PS3 and PS4;
  see [PS3 / PS4 motion](Firmware/RP2040/docs/PS3_PS4_Motion_Controls.md)). PS5 games and Xbox
  One / Series are not supported. On a **PC**, PS4 mode is a complete DualShock 4 and needs nothing
  else.
- **Several controllers on one adapter** (`MAX_GAMEPADS` > 1) is only tested in Wii U mode; other
  modes are not supported with more than one controller yet ([roadmap](docs/ROADMAP.md#4-several-controllers-on-one-adapter)).
  For local multiplayer on an Xbox 360, use one adapter per player: each adapter gets its own
  USB / XSM3 identity from the board's unique ID. On other consoles two adapters share the same
  USB identity, so they may not show up as two players.
- **RP2040 / Pico 2 + ESP32 hybrid** (first bench test with a Pico 2 W, an ESP32 and a
  DualShock 4 on a PC; the features below are built and unit-tested, their hardware test is
  [planned](docs/HYBRID_TEST_PLAN.md)): the USB modes marked in the [mode table](#output-modes)
  only (the GPIO modes would take over the I2C pins; Wii needs the Pico W radio). The ESP32 runs
  the same Bluetooth code as the Pico W (`Firmware/common/Bluepad32`): Joy-Con pairs, motion,
  touchpad, battery, lightbar, combos, search policy, idle turn-off and the diagnostics report
  reach the RP2040 over I2C, and the RP2040 keeps the settings (the web app over USB or over
  Bluetooth edits the same ones). The diagnostics report has the link details too, an optional USB
  host port takes wired controllers and pairs a DualShock 3, and both boards are flashed and their
  logs read through the adapter's own USB port ([Tools/hybrid](Tools/hybrid/README.md)). Switch 2
  and Steam Controller 2026 pads are untested on the original ESP32's Bluetooth LE 4.2.
  Plan: [roadmap](docs/ROADMAP.md#6-rp2040--esp32-hybrid).
- **Wii mode** takes a wired (USB) controller only; the radio is busy talking to the console.
- **Bluetooth on the Pico W / Pico 2 W** uses the on-module antenna, which has little margin with
  your body between the controller and the adapter. Keep a line of sight or use a USB extension.

## Roadmap

1. **Turbo / auto-fire** and **button macros**, set in the web app
2. **Native mode** — each controller shows up as itself, with report conversion where needed
3. **Several controllers on one adapter** — up to 4 Bluetooth players, and a co-pilot mode
4. **Bluetooth out where a system only takes Bluetooth** — Bluetooth controllers in Wii mode, Wii U
   Pro Controller, Apple TV
5. **RP2040 + ESP32 hybrid** — hardware testing of the features it now shares with the Pico W

Details and status: [docs/ROADMAP.md](docs/ROADMAP.md). Known bugs: [docs/TODO.md](docs/TODO.md).

## Building from source

Full walkthrough (tools, submodules, Pico SDK 2.1.0, build scripts, manual CMake, flashing, the
ESP32 companion): [Building from source](Firmware/RP2040/docs/Building_From_Source.md).

**Quick version** — interactive scripts that check prerequisites and ask for the board, the mode
set and Release / Debug; output goes to `scripts/build/`:

| Platform | Command (from the repository root) |
|---|---|
| Linux / macOS | `./scripts/build.sh` |
| Windows (PowerShell) | `.\scripts\build.ps1` |

Or with CMake directly (Pico 2 W, Release):

```sh
git clone --recursive https://github.com/felipearmat/OGX-Mini-improved.git
cd OGX-Mini-improved/Firmware/RP2040
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOGXM_BOARD=PI_PICO2W -DMAX_GAMEPADS=1
cmake --build build
```

**Boards (`OGXM_BOARD`):** `PI_PICO`, `PI_PICO2`, `PI_PICOW`, `PI_PICO2W`, `RP2040_ZERO` (Waveshare),
`RP2350_ZERO` (Waveshare), `RP2350_USB_A` (Waveshare), `RP2040_XIAO` (Seeed), `RP2354`
(RP2350 + Pi Radio Module 2: Bluetooth like the Pico 2 W, USB-A on GP0 / GP1, LED GP25),
`ADAFRUIT_FEATHER` (controller on the board's USB-A port; plug it in after boot if it isn't
detected), `OGX_LIVE`, `EXTERNAL_4CH_I2C` (4-channel), `ESP32_BLUEPAD32_I2C` (RP2040 + ESP32
hybrid), `ESP32_BLUEPAD32_I2C_PICO2` (the same hybrid on a Pico 2 / Pico 2 W; the Pico 2 W radio
is not used), `ESP32_BLUERETRO_I2C` (legacy: the ESP32 runs the third-party BlueRetro firmware,
outside this project's shared Bluetooth code; BlueRetro was archived upstream in December 2025). Maintainer-tested: Pico 2 W (this fork); Feather USB Host, Pico W,
Pico 2 W, RP2350-USB-A and OGX Live (upstream).

**Options:** `MAX_GAMEPADS` (1–4, default 1; see [Limitations](#limitations)),
`OGXM_FIXED_DRIVER` / `OGXM_FIXED_DRIVER_ALLOW_COMBOS` (a single-mode build),
`MAIN_LOOP_DELAY_US` (default 0, e.g. 250 for lower CPU use), the
[adapter option defaults](#web-app-and-adapter-options), and for the Pico 2 W external RGB LED
`OGXM_EXT_RGB_PIN` (default 6, -1 = none), `OGXM_EXT_RGB_COUNT` (1–4, default 1),
`OGXM_EXT_RGB_BRIGHTNESS` (1–255, default 48).

**ESP32 companion** (Bluetooth side of `ESP32_BLUEPAD32_I2C` and `ESP32_BLUEPAD32_I2C_PICO2`): ESP-IDF 5.5 or newer, then
`cd Firmware/ESP32 && idf.py build`. Wiring: [hardware/](hardware/README.md#pi-pico--esp32).

**Latency:** the adapter sends the latest pad state whenever the USB endpoint is free and the main
loop has no added delay by default, so the host gets updates at its poll rate
([details](Firmware/RP2040/docs/IMPROVEMENTS.md#latency-reduction)).

**Contributing:** [Firmware architecture](Firmware/RP2040/docs/Firmware_Architecture.md) ·
[Adding supported controllers](Firmware/RP2040/docs/Adding_Supported_Controllers.md) (new
controllers get a dedicated driver; don't change a working one) ·
[Unit tests and CI](docs/TESTING.md) · [Code shared by both firmwares](Firmware/common/README.md).

## Getting help and support

- **Bugs and questions about this fork:** open an issue on
  [felipearmat/OGX-Mini-improved](https://github.com/felipearmat/OGX-Mini-improved/issues) with the
  details asked for in [Support issue requirements](Firmware/RP2040/docs/Support_Issue_Requirements.md)
  and, for lag or disconnects, a [log report](#diagnostics-and-log-report). This fork is tested on a
  Raspberry Pi Pico 2 W.
- **Boards and controllers you can't get tested here:** pull requests with fixes tested on your own
  hardware are welcome.

<details>
<summary>The original project's support policy (MegaCadeDev/OGX-Mini-2026)</summary>

Development focuses on **first-party controllers** that can be tested on hardware the maintainer
owns. Maintainer work is limited to first-party pads (Microsoft, Sony, Nintendo) unless a donation
is provided specifically to buy that controller for testing, or there is an existing agreement to
ship hardware. Unsupported third-party controllers are not diagnosed by guesswork without that
hardware.

Boards tested and maintained upstream: [Adafruit Feather USB Host](https://www.adafruit.com/product/5723),
Raspberry Pi Pico W, Pico 2 W, Waveshare RP2350-USB-A and OGX Live. For other boards (RP2040-Zero,
RP2350-Zero, XIAO, RP2354, ESP32 hybrids, 4-channel setups, clones): fork, test your patches on your
hardware and open a pull request. Unofficial clones and homebrew PCBs that only look like a
supported board are not guaranteed to work.

Upstream: [Ko-fi](https://ko-fi.com/megacadedev) · [Discord](https://discord.gg/HhZuSaSc4) ·
[TikTok](https://www.tiktok.com/@ogxmini2026) · [YouTube](https://www.youtube.com/@ogxmini2026)

</details>

## Documentation

| For | Where |
|---|---|
| Using a mode | [Steam / SteamOS / Bazzite](Firmware/RP2040/docs/SteamOS_Bazzite_Output_Mode.md) · [PS3 / PS4 motion](Firmware/RP2040/docs/PS3_PS4_Motion_Controls.md) · [Wii](Firmware/RP2040/docs/Wii_Mode_Guide.md) · [Wii with a USB controller on a Pico 2 W](Firmware/RP2040/docs/PICO2W_WII_USB_SETUP.md) · [GPIO pinouts](Firmware/RP2040/docs/GPIO_Output_Pinout_and_Mappings.md) · [Button mappings](Firmware/RP2040/docs/Controller_Mappings.md) · [Web app guide](Firmware/RP2040/docs/Web_App_Guide.md) |
| Controllers | [Wired controllers](Firmware/RP2040/docs/Wired_Controllers.md) · [GameSir Cyclone 2](Firmware/RP2040/docs/GameSir_Cyclone2.md) · [GameSir G7 Pro](Firmware/RP2040/docs/GameSir_G7Pro.md) · [Flydigi APEX 4](Firmware/RP2040/docs/Flydigi_APEX4_Wukong.md) · [Victrix Gambit](Firmware/RP2040/docs/Victrix_Gambit.md) |
| Hardware | [Boards and wiring](hardware/README.md) |
| Plans and changes | [Roadmap](docs/ROADMAP.md) · [Known issues](docs/TODO.md) · [Changelog](CHANGELOG.md) · [Fork commits offered upstream](docs/UPSTREAM.md) · [Upstream wish list and research](Firmware/RP2040/docs/Planned_Additions.md) |
| Development | [Building from source](Firmware/RP2040/docs/Building_From_Source.md) · [Firmware architecture](Firmware/RP2040/docs/Firmware_Architecture.md) · [Adding controllers](Firmware/RP2040/docs/Adding_Supported_Controllers.md) · [Unit tests and CI](docs/TESTING.md) · [Shared code](Firmware/common/README.md) · [Technical notes per feature](Firmware/RP2040/docs/IMPROVEMENTS.md) |
| Everything | [Firmware docs index](Firmware/RP2040/docs/README.md) · [docs/](docs/README.md) |

## Credits and licenses

Based on [wiredopposite's OGX-Mini](https://github.com/wiredopposite/OGX-Mini) and
[MegaCadeDev's OGX-Mini-2026](https://github.com/MegaCadeDev/OGX-Mini-2026).

Projects this fork builds on or learned from:

- **[Bluepad32](https://github.com/ricardoquesada/bluepad32)** — Bluetooth controller support.
- **[Joypad OS](https://github.com/joypad-ai/joypad-os)** — reference for Xbox 360 (XSM3)
  authentication with retail consoles; XInput descriptors and the XSM3 flow are aligned with it.
- **[Sakura-Research-Lab/OGX-Mini-2026-Testing](https://github.com/Sakura-Research-Lab/OGX-Mini-2026-Testing)**
  — OG Xbox Guide button (tap / hold IGR / shutdown), RP2350_ZERO, RP2040_XIAO and RP2354 board
  support, 8BitDo XInput LED keepalive fix.
- **[faithvoid](https://github.com/faithvoid)** — Xbox 360 controller rumble fix (host-side rumble,
  including the wireless RUMBLE_ENABLE sequence).
- **[PicoGamepadConverter](https://github.com/wiredopposite/PicoGamepadConverter)** — Wii (Wiimote)
  output approach and the No Extension / Nunchuk / Classic Controller mappings.
- **[retro-pico-switch](https://github.com/DavidPagels/retro-pico-switch)** — Switch Pro Controller
  protocol and report layout.
- **[Nadeflore/switch2-controllers](https://github.com/Nadeflore/switch2-controllers)**,
  **[TommyWabg/switch2-controllers-windows10-gyro](https://github.com/TommyWabg/switch2-controllers-windows10-gyro)**
  and **[BlueRetro #1249](https://github.com/darthcloud/BlueRetro/issues/1249)** — Switch 2 Bluetooth
  LE discovery, pairing, reports and rumble.
- **[SDL3 `SDL_hidapi_steam_triton`](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_steam_triton.c)**
  / [`controller_structs.h`](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/steam/controller_structs.h)
  — Steam Controller 2026 (Triton) reports, buttons and rumble.

Licenses: Xbox 360 console authentication uses [libxsm3](https://github.com/InvoxiPlayGames/libxsm3)
(LGPL 2.1 or later; credits oct0xor, emoose (ExCrypt) and sanjay900; see
`Firmware/external/libxsm3/`). Other dependencies (Bluepad32, BTstack, TinyUSB, Pico SDK and more):
the root `LICENSE` file and `Firmware/external/`.
