# Web app guide: checking a controller and sending a diagnostics report

The [web app](https://felipearmat.github.io/OGX-Mini-improved-WebApp/) changes mappings and deadzones, and it is also the easiest way to see what the adapter receives from a controller. Its **Diagnostics** panel shows how well each controller's link is doing, and **Generate log report** saves a file to attach when [opening a support issue](Support_Issue_Requirements.md).

**Related**

| Topic | Document |
|-------|----------|
| What a support issue must include | [Support_Issue_Requirements.md](Support_Issue_Requirements.md) |
| Input → output mapping reference | [Controller_Mappings.md](Controller_Mappings.md) |

---

## Connecting

1. Plug the adapter into a PC.
2. Put it in **D-Input/Debug mode**: hold **Start + Right Bumper + D-pad Left** for about 3 seconds. The adapter restarts in that mode.
3. Open the web app in a Chromium-based browser (Chrome, Edge, Brave): Web Serial is not available in Firefox or Safari.
4. Click **Connect via USB** and pick the OGX-Mini.

The Diagnostics panel works over USB only. Over Bluetooth, the mapping settings are available but not the report.

---

## Checking a controller

With the controller connected to the adapter:

- **Buttons:** pressing a button lights up its row in Digital Mappings (and Analog Mappings), by the physical button, whatever it is mapped to. A button that does not light up does not reach the adapter.
- **Sticks and triggers:** the Axis Settings panel draws each stick and trigger live: a **grey** dot (or mark) where the controller is, and a **green** one with the deadzone, curve and other settings shown on the page (what the game gets). A stick that rests off center, or does not reach the edge, shows up in the grey dot.
- **Touchpad and motion:** the Touchpad and Motion panel draws the fingers on a DS4 / DualSense touchpad (pressing it lights up its row) and, below, a box that turns with the controller (press **Start**, lay the controller flat, **Recentre**), with the accelerometer (g) and gyro (deg/s) values. A box that turns the wrong way or drifts while the controller lies still points to a motion problem.
- **Rumble:** the Rumble Test panel runs each motor (in the output modes that pass rumble to the controller), so you can tell whether the motors work and are the right way round.
- **Link health:** open **Diagnostics** and click **Refresh** while moving the sticks (some controllers only report when something changes). Each controller gets a row:

| Column | Meaning |
|--------|---------|
| Link | Classic or LE Bluetooth, the LE connection interval, power saving (sniff) if the controller is in it; USB speed for wired controllers |
| Reports/s | Input reports per second the adapter receives |
| Late | Share of reports that arrived more than twice the usual interval after the previous one (last 5-10 s) |
| Lost | Reports the controller numbered but the adapter never got (DualShock 4 and DualSense, which count their reports) |
| Largest gap | Longest time without a report in the last 5-10 s |
| Signal | Bluetooth signal (dBm for LE; for Classic, "good" or how far it is from the receiver's ideal range), the radio channels in use, and the adapter's transmit power on the link (current / maximum). Only with **Detailed controller diagnostics** on (top of the Diagnostics panel; it needs **Keep the log in flash** on too) |
| Connected | Time since the controller connected |

Lost works for the DualShock 4, the DualSense and Switch pads (Joy-Con, Pro Controller), which number their reports. Any stretch of 60 ms or more without input is also listed in the report's events ("no input for X ms").

As a guide, a DualShock 4 over Bluetooth next to the adapter shows about 250 reports/s, under 1 % late and lost, and a largest gap under 30 ms. Gaps of 100 ms or more are felt as stutter in a game. High late / lost values usually come from distance, obstacles or 2.4 GHz interference (Wi-Fi, other wireless devices), a low battery, or the adapter searching for new controllers (shown above the table).

---

## When to generate a report

Generate a report and attach it to the issue whenever the problem is about how the adapter behaves, for example:

- input lag, stutter, or buttons that sometimes do not register;
- a controller that disconnects, does not pair, or does not reconnect;
- the adapter freezing, restarting by itself, or ending up in the wrong mode.

It is not a replacement for the adapter-side report capture required for [mapping problems and new controllers](Adding_Supported_Controllers.md#step-2--capture-reports-on-the-adapter-required-for-driver-mapping).

---

## How to generate a report

1. Use the adapter in the mode you play with until the problem happens.
2. **Without unplugging it**, switch to D-Input/Debug mode (**Start + Right Bumper + D-pad Left**, about 3 s).
3. Connect the web app over USB, open **Diagnostics** and click **Generate log report**. A file named `ogx-mini-report-<date>.json` is saved.
4. Attach that file to the issue, and say which mode and controller the problem happened with.

Measurements of the mode you played with are kept across the switch into D-Input/Debug mode, so the report covers them (`previous_session`). If the adapter was unplugged in between, generate the report anyway: the summary is also stored on the adapter when you change modes and when the last controller turns off or is disconnected with Start + Select (`stored_session`). If the adapter crashed, it restarts by itself and the report includes what it recorded (`last_crash`).

### What the report contains

The adapter's board, firmware version and build, output mode, and uptime; for each controller its name, USB / Bluetooth IDs, its battery level (and, for a DualShock 4 / DualSense, whether it is charging), the first half of its Bluetooth address (the manufacturer part only), its Bluetooth chip vendor and version, its link and the timing figures above; the latest motion sample of a controller with motion sensors; a list of recent events (connections, disconnections with the reason, mode changes, searches for new controllers); the summary of the previous session; the log kept in flash when **Keep the log in flash** is on; and the browser's version. It contains no personal data.
