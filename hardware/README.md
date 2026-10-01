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
This is the minumum amount of connections you'll want for this to work and the diagram assumes you're powering the Pico and ESP32 each separately via USB (do not connect power between the 2 boards if so). A more complex configuration is possible, making the Pi Pico able to program the ESP32, but I'll update the repo with that diagram later.
![OGX-Mini](../images/DiagramPicoESP32.png)

# RP2040-Zero
![OGX-Mini](../images/DiagramRPZero.png)

Gerber, BOM, and schematic for an RP2040-Zero interposer board you can make yourself. LED1 and R3 are both optional. 

The RP2040-Zero board can be found on Amazon and AliExpress.

![OGX-Mini Boards](../images/OGX-Mini-rpzero-int.jpg "OGX-Mini Boards")