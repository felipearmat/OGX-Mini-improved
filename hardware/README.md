# Pi Pico
![OGX-Mini](../images/DiagramPico.png)

# Pi Pico 2
The Pico 2 will likely require 4.7k resistors between the USB data lines and ground to work correctly.
![OGX-Mini](../images/DiagramPico2.png)

# Pico 2 W external RGB status LED
Optional. One WS2812B (a 5 V addressable LED, e.g. cut from a strip) shows the output mode on the
Pico 2 W: it blinks the mode number at boot, blinks while pairing and stays on when a controller is
connected, in the mode colour (XInput green, Switch red, DInput blue, PS4 light blue, STEAM purple,
original Xbox yellow, PS3 dark blue, others white). The onboard LED keeps working as before.

| WS2812B | Pico 2 W |
|---|---|
| 5V | VBUS (pin 40) |
| GND | GND (pin 8 or 38) |
| DIN | GP6 (pin 9) through a 330 Ω resistor (220 to 470 Ω) |

- Wire to the **DIN** side (strips print an arrow from DIN to DOUT).
- More LEDs can be chained (DOUT to the next DIN, 5V and GND in parallel) for later use; the status
  is shown on the first one and the others stay off. Set `OGXM_EXT_RGB_COUNT` to how many are chained.
- The data line is 3.3 V and the LED expects 3.5 V at 5 V supply. With short wires this works in
  practice; if colours flicker or are wrong, power the first LED through a 1N4148 diode (anode on
  VBUS, cathode on the LED's 5V) so 3.3 V is a valid high for it.
- Build options: `OGXM_EXT_RGB_PIN` (default `6`, `-1` = none), `OGXM_EXT_RGB_COUNT` (1 to 4),
  `OGXM_EXT_RGB_BRIGHTNESS` (1 to 255, default `48`). GP6 is free in every mode (debug UART is
  GP4/GP5, PIO USB GP0/GP1, GPIO modes GP10, GP11, GP19 to GP22 and GP26). The LED uses PIO block 2.

# Pi Pico + ESP32
The RP2040 + ESP32 hybrid: the Pico handles USB, an **original ESP32** (ESP32-WROOM-32 / -32U;
the S3, C3 and C6 have no Bluetooth Classic) handles the Bluetooth controllers, over I2C. Firmware:
`OGXM_BOARD=ESP32_BLUEPAD32_I2C` for an RP2040 Pico, `ESP32_BLUEPAD32_I2C_PICO2` for a Pi Pico 2
or Pico 2 W (same header pinout; on a Pico 2 W the on-board radio stays off and Bluetooth comes from
the ESP32), plus the ESP32 companion in `Firmware/ESP32`. First bench test done; the full hardware test is [planned](../docs/HYBRID_TEST_PLAN.md).

![OGX-Mini](../images/DiagramPicoESP32.png)

Everything goes through the Pico's USB port: the console / PC link, flashing both boards and
reading both boards' logs ([Tools/hybrid](../Tools/hybrid/README.md)). The ESP32's own USB port is
never needed: its ROM bootloader is reached through the Pico, even on a blank ESP32.

| Pico / Pico 2 / Pico 2 W | ESP32 | |
|---|---|---|
| GP18 (pin 24) | GPIO21 | I2C SDA: controllers, settings, diagnostics |
| GP19 (pin 25) | GPIO22 | I2C SCL |
| GP8 (pin 11) | EN | restarts the ESP32 |
| GP20 (pin 26) | GPIO0 (IO0 / BOOT) | puts the ESP32 in its bootloader for flashing |
| GP16 (pin 21, UART0 TX) | GPIO3 (RX0) | flashing data to the ESP32 |
| GP17 (pin 22, UART0 RX) | GPIO1 (TX0) | flashing replies and the ESP32's log |
| VBUS (pin 40) | VIN / 5V | one USB cable powers both boards (see below) |
| GND | GND | required |
| — | 2.2 kΩ from GPIO21 to 3V3, 2.2 kΩ from GPIO22 to 3V3 | I2C pull-ups (4.7 kΩ also works); the internal ones are too weak for the 1 MHz link |

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
with the adapter in Web App mode: `bootsel --uf2 FILE` updates the Pico (a blank Pico starts in its
USB bootloader by itself), `flash-esp32 BUILD_DIR` flashes the ESP32 through GP16 / GP17 / GP20 /
GP8, `monitor` shows the log both boards kept from the last session, then the ESP32's live
output.

# RP2040-Zero
![OGX-Mini](../images/DiagramRPZero.png)

Gerber, BOM, and schematic for an RP2040-Zero interposer board you can make yourself. LED1 and R3 are both optional. 

The RP2040-Zero board can be found on Amazon and AliExpress.

![OGX-Mini Boards](../images/OGX-Mini-rpzero-int.jpg "OGX-Mini Boards")