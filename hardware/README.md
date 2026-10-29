**Soldering:** take care with the boards' corner pads (on a Pico: pins 1, 20, 21 and 40). They are
the ones usually tacked first and pulled by the wires, and a poor or too hot iron often lifts or
melts them. The wiring this fork adds keeps off them where it can (the hybrid's UART TX is GP28,
pin 34).

# Pi Pico
![OGX-Mini](../images/DiagramPico.png)

# Pi Pico 2
The Pico 2 will likely require 4.7k resistors between the USB data lines and ground to work correctly.
![OGX-Mini](../images/DiagramPico2.png)

# External RGB status LED
Optional. One WS2812B (a 5 V addressable LED, e.g. cut from a strip) shows the adapter's status in
the output mode colour (the [mode table](../README.md#output-modes) lists each one: Nintendo
from magenta to orange, Xbox greens, PlayStation from cyan to lilac, white for D-Input/Debug; any
mode's colour can be changed in the web app): it blinks the mode number at boot, blinks while
searching for controllers (fast; slow for the reduced search) and stays on when it stops (a
controller connected and no free slot). The onboard LEDs keep working as before.

Set it in the [web app](../Firmware/RP2040/docs/Web_App_Guide.md), Adapter Options: each LED on or off (the Pico's own,
the external one on the Pico, the ESP32's and the external one on the ESP32, as the board has
them) and the RGB LEDs' brightness; saving restarts the adapter, and both boards start with them.
Each mode's colour is picked under Device Mode, with a live preview on the LED and the
controller's lightbar. Tested on the RP2040 + ESP32 adapter (a Pico 2 W + an ESP32 DevKit), with
an LED on GP6 and one on the ESP32's GPIO 4.

Where to solder it:

| Board | LED on | DIN pin |
|---|---|---|
| Pico 2 W | the Pico | GP6 (pin 9) |
| [ESP32 hybrid](#pi-pico--esp32) with a Pico 2 / Pico 2 W | the Pico **or** the ESP32 | GP6 (pin 9) / ESP32 GPIO 4 |
| ESP32 hybrid with an RP2040 Pico | the ESP32 | ESP32 GPIO 4 |

Both firmwares drive their pin whether or not an LED is there, so one LED on either board works.

| WS2812B | Pico | ESP32 |
|---|---|---|
| 5V | VBUS (pin 40) | 5V (VIN) |
| GND | GND (pin 8 or 38) | GND |
| DIN | GP6 through a 330 Ω resistor (220 to 470 Ω) | GPIO 4 through a 330 Ω resistor |

- Wire to the **DIN** side (strips print an arrow from DIN to DOUT).
- The data line is 3.3 V and the LED expects 3.5 V at 5 V supply. With short wires this works in
  practice; if colours flicker or are wrong, power the LED through a 1N4148 diode (anode on 5V,
  cathode on the LED's 5V) so 3.3 V is a valid high for it.
- Pico build options: `OGXM_EXT_RGB_PIN` (default `6`, `-1` = none), `OGXM_EXT_RGB_COUNT` (1 to 4;
  more LEDs can be chained, DOUT to the next DIN, for later use: the status is on the first one),
  `OGXM_EXT_RGB_BRIGHTNESS` (1 to 255, default `48`; the web app's brightness replaces it). GP6 is free in every mode (debug UART is
  GP4/GP5, PIO USB GP0/GP1, GPIO modes GP10, GP11, GP19 to GP22 and GP26; the hybrid's ESP32 wires
  use GP8, GP16 to GP21 and GP28). The LED uses PIO block 2, which the RP2040 does not have.
- ESP32 options (`idf.py menuconfig`, *OGXMini Options*): `ENABLE_EXT_RGB` (on), `EXT_RGB_PIN`
  (default `4`), `EXT_RGB_BRIGHTNESS` (default `48`). The LED uses the RMT peripheral. On the
  hybrid the RP2040 keeps the settings: the ESP32 gets the web app's on / off, brightness and the
  mode colour from it.

# Pi Pico + ESP32
The RP2040 + ESP32 hybrid: the Pico handles USB, an **original ESP32** (ESP32-WROOM-32 / -32U;
the S3, C3 and C6 have no Bluetooth Classic) handles the Bluetooth controllers, over I2C. Firmware:
`OGXM_BOARD=ESP32_BLUEPAD32_I2C` for an RP2040 Pico, `ESP32_BLUEPAD32_I2C_PICO2` for a Pi Pico 2
or Pico 2 W (same header pinout; on a Pico 2 W the on-board radio stays off and Bluetooth comes from
the ESP32), plus the ESP32 companion in `Firmware/ESP32`. Tested on a bench (a Pico 2 W and an ESP32 DevKit on jumpers, one controller at a time): results in the [test plan](../docs/HYBRID_TEST_PLAN.md).

![OGX-Mini](../images/DiagramPicoESP32.png)

**Power the ESP32 from the Pico's VBUS** (VBUS to the ESP32's 5V / VIN), so both boards power up
and down together. If the ESP32 browns out on VBUS when its radio starts ("E BOD: Brownout" in the
log), use a shorter cable, fewer devices on the hub or a USB port that gives more current. An ESP32
powered from its own USB port keeps its TX0 and the I2C pull-ups at 3.3 V while the Pico is
unplugged; if the Pico does not come back on USB, unplug both and plug the Pico in first.

Everything goes through the Pico's USB port: the console / PC link, flashing both boards and
reading both boards' logs ([Tools/hybrid](../Tools/hybrid/README.md)). The ESP32's own USB port is
never needed: its ROM bootloader is reached through the Pico, even on a blank ESP32.

| Pico / Pico 2 / Pico 2 W | ESP32 | | CMake option |
|---|---|---|---|
| GP18 (pin 24) | GPIO21 | I2C SDA: controllers, settings, diagnostics | `OGXM_HYBRID_I2C_SDA_PIN` |
| GP19 (pin 25) | GPIO22 | I2C SCL | `OGXM_HYBRID_I2C_SCL_PIN` |
| GP8 (pin 11) | EN | restarts the ESP32 | `OGXM_HYBRID_ESP32_EN_PIN` |
| GP20 (pin 26) | GPIO0 (IO0 / BOOT) | puts the ESP32 in its bootloader for flashing | `OGXM_HYBRID_ESP32_IO0_PIN` |
| GP28 (pin 34, UART0 TX) | GPIO3 (RX0) | flashing data to the ESP32. GP16 (pin 21) carries the same signal, for hybrids wired before (the diagram still shows GP16); pin 21 is a corner pad, often the one damaged by a poor soldering iron | `OGXM_HYBRID_UART_TX_PIN`, `OGXM_HYBRID_UART_TX_ALT_PIN` (`-1` = none) |
| GP17 (pin 22, UART0 RX) | GPIO1 (TX0) | flashing replies and the ESP32's log | `OGXM_HYBRID_UART_RX_PIN` |
| VBUS (pin 40) | VIN / 5V | one USB cable powers both boards (see below) | |
| GND | GND | required | |
| — | 2.2 kΩ from GPIO21 to 3V3, 2.2 kΩ from GPIO22 to 3V3 | I2C pull-ups (4.7 kΩ also works with short wires); the internal ones are too weak for the link (the ESP32 writes at 1 MHz and reads at 400 kHz). With longer jumpers use 1 kΩ (or a second 2.2 kΩ in parallel): slow edges show up as `i2c_read_errors` and `settings_replies_cut` in the diagnostics report | |

**Other pins:** every Pico-side wire above, the USB host port's D+ (`OGXM_HYBRID_USB_DP_PIN`, D- is
the next GPIO) and the UART-bridge jumper (`OGXM_HYBRID_BRIDGE_JUMPER_PIN`) can be moved when you
build the firmware yourself, e.g. for a damaged pad or a board layout with fewer crossing wires:

```bash
cmake ... -DOGXM_BOARD=ESP32_BLUEPAD32_I2C_PICO2 -DOGXM_HYBRID_I2C_SDA_PIN=26 -DOGXM_HYBRID_I2C_SCL_PIN=27
```

The configure step stops on a pin that cannot do the job or is taken twice: SDA / SCL must be an
SDA and an SCL of the same I2C controller (e.g. 18 / 19, 26 / 27, 6 / 7), TX / RX must be UART0
(TX GP0, 12, 16 or 28; RX GP1, 13, 17 or 29), and GP23, 24, 25 and 29 (the Pico W radio, a Pico's
power and LED pins), the external LED's pin and, in Debug builds, GP4 / GP5 are taken. Tested on hardware with I2C on GP26 / GP27 and IO0 on GP22. The release
UF2s use the defaults above. The ESP32 side does not move.

On a **Pico W / Pico 2 W**, build with `-DOGXM_HYBRID_PICO_W=ON`: GP25 is the radio chip's line there
(the hybrid keeps the radio off), so there is no onboard status LED and GP25 is left alone.

**Perfboard layout with few crossing wires** (38-pin ESP32 DevKit, the boards on the two faces
of a double-sided perfboard, USB ports at opposite ends): the ESP32's signal column (from the
antenna end: 22, TX0, RX0, 21, GND, ..., 0) then faces the Pico's GP0-GP15 column in the order
below, and EN / 5V / GND, on the ESP32's other column, go on the other face. Build with
`-DOGXM_HYBRID_ESP32_IO0_PIN=9 -DOGXM_HYBRID_I2C_SDA_PIN=10 -DOGXM_HYBRID_I2C_SCL_PIN=11
-DOGXM_HYBRID_UART_TX_PIN=12 -DOGXM_HYBRID_UART_RX_PIN=13`:

| Pico | ESP32 |
|---|---|
| GP9 (pin 12) | GPIO0 (IO0) |
| GND (pin 13) | GND |
| GP10 (pin 14) | GPIO21 (SDA) |
| GP11 (pin 15) | GPIO22 (SCL): crosses the two UART wires; GP15 (pin 20, `-DOGXM_HYBRID_I2C_SCL_PIN=15`) avoids that, but it is a corner pad |
| GP12 (pin 16) | RX0 |
| GP13 (pin 17) | TX0 |
| GP8 (pin 11) | EN (other face) |

Not built on hardware yet.

Optional, on the Pico:

| Pico | | |
|---|---|---|
| GP0 (pin 1) / GP1 (pin 2) | USB-A socket D+ / D- (5 V from VBUS, GND) | USB host port: wired controllers, and a DualShock 3 plugged in once to pair it with the ESP32 |
| GP4 (pin 6) / GP5 (pin 7) | USB-serial adapter RX / TX | the RP2040's log in Debug builds (also kept in RAM and read over USB) |
| GP21 (pin 27) | jumper to GND at boot | UART-bridge mode by hand, if the USB command can't be used |

The diagram shows a 38-pin ESP32 board; a 30-pin board has the same GPIO21, GPIO22, GPIO0, RX0,
TX0, EN, 3V3 and GND pins.

**Power:** one USB cable on the Pico with **VBUS (pin 40) to the ESP32's VIN / 5V** — the two
boards draw well under the 500 mA of a USB port. Don't plug the ESP32's own USB at the same time.
While the boards are separate (design work), each can have its own USB cable instead; then don't
connect VBUS.

**Flashing and logs through the Pico** ([Tools/hybrid/ogxm_hybrid.py](../Tools/hybrid/README.md)),
with the adapter in D-Input/Debug mode: `bootsel --uf2 FILE` updates the Pico (a blank Pico starts in its
USB bootloader by itself), `flash-esp32 BUILD_DIR` flashes the ESP32 through GP28 (or GP16) / GP17 / GP20 /
GP8, `monitor` shows the log both boards kept from the last session, then the ESP32's live
output.

# RP2040-Zero
![OGX-Mini](../images/DiagramRPZero.png)

Gerber, BOM, and schematic for an RP2040-Zero interposer board you can make yourself. LED1 and R3 are both optional. 

The RP2040-Zero board can be found on Amazon and AliExpress.

![OGX-Mini Boards](../images/OGX-Mini-rpzero-int.jpg "OGX-Mini Boards")