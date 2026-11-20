# ESP8266 Wi-Fi Repeater (Wemos D1 mini Pro)

Wi-Fi repeater with a 0.96" SSD1306 OLED, six buttons, battery monitor, NeoPixel status LED and a web UI.
Traffic is forwarded with lwIP NAPT, so clients of the repeater sit on their own subnet behind the ESP.

## Default wiring

Buttons connect the pin to GND (internal pull-ups). Every pin can be changed in the web UI (Device > Pins).

| Function  | GPIO | D1 mini label |
|-----------|------|---------------|
| OLED SDA  | 4    | D2            |
| OLED SCL  | 5    | D1            |
| Up        | 14   | D5            |
| Down      | 12   | D6            |
| Left      | 13   | D7            |
| Right     | 0    | D3            |
| OK        | 2    | D4            |
| Back      | 3    | RX            |
| NeoPixel  | 15   | D8            |
| Battery   | A0   | A0 (fixed)    |

Notes:

- GPIO0 and GPIO2 must be high at boot, which is the case while their buttons are released.
- GPIO3 (RX) is shared with the USB serial chip. The firmware never uses the serial port, so it is free as a button. If flashing misbehaves, unplug that button or move it.
- GPIO15 (D8) is pulled low on the board, which a WS2812 data line tolerates. If the LED misbehaves at 3.3 V data, add a level shifter or a 74AHCT125.
- GPIO16 has no internal pull-up and is not available for the OLED or the LED.
- The OLED must be an I2C module at address 0x3C.

## Battery

The battery is read on A0 through your voltage divider. The D1 mini Pro already has a 220k/100k divider on A0, so an extra series resistor from the battery is usually all that is added.

1. Flash and open the web UI, Device > Battery.
2. Measure the battery with a multimeter, type the value into "Calibrate" and press the button.

Calibration sets the "voltage that reads as full scale" value, so it accounts for your exact resistors. The default (4200 mV) matches a 100k series resistor with the onboard divider.

Percent comes from a typical single-cell Li-ion discharge curve. Below 2.5 V (for example when running from USB with no battery) the screen shows USB instead of a battery. The firmware cannot tell if the battery is charging.

## Status LED

Pick what the NeoPixel shows in the web UI or on the device (Menu > LED):

| Mode        | Behaviour |
|-------------|-----------|
| Off         | LED dark |
| Connection  | Green: connected and repeating. Blue: connected, repeater off. Amber pulse: connecting. Red pulse: failed or link lost. Violet pulse: no network saved yet |
| Battery     | Green 60% and up, amber 30-59%, orange 15-29%, red flashing below 15% |
| Alternate   | Connection colour for three seconds, battery colour for one |

Brightness (5-100%) and the number of pixels (1-8, all showing the same colour) are configurable.

## Using the device

| Button       | Action                                                          |
|--------------|-----------------------------------------------------------------|
| Up / Down    | Move selection (hold to repeat)                                 |
| Left / Right | Change a value, or move on the keyboard                         |
| OK           | Open / confirm / press the highlighted key                      |
| Back         | Go back. On the keyboard: delete a character                    |
| Hold Back    | Jump to the home screen. On the keyboard: cancel                |

Home screen: link quality, channel and signal strength, battery, upstream network and address, and a live path
uplink -> repeater -> clients that animates while traffic can flow.

Menu: Networks (scan, pick, type password), Repeater (on/off), Display (brightness, timeout, flip), LED, Info, Forget network, Restart.

Keyboard keys: `abc` / `ABC` / `123` / `#+=` switch the character page, `SPC` space, `DEL` delete, `SHOW` / `HIDE` reveal the password, `OK` connects (minimum 8 characters).

## Web UI

1. Join the repeater's own network. Defaults: name `ESP-Repeater`, password `repeater123`.
2. Open `http://192.168.4.1` (also shown under Menu > Info).
3. Overview: live copy of the OLED, signal / battery / client readouts, 3-minute signal history, connected devices. Connect, Access point and Device tabs hold the settings.

The web UI only answers clients on the repeater's own network, not devices on the upstream Wi-Fi.
Change the default access point password first.

If the upstream network also uses 192.168.4.x, the repeater moves its own network to 192.168.45.x automatically.

## Build

GitHub: push this folder to a repository. `.github/workflows/build.yml` builds on every push and uploads `wifi-repeater-d1-mini-pro.bin` as an artifact. A tag such as `v1.1.0` also attaches it to a release.

Local: `pio run`.

## Flash

```
esptool.py --chip esp8266 --baud 460800 write_flash 0x0 wifi-repeater-d1-mini-pro.bin
```

Run `esptool.py erase_flash` once first. Settings from version 1.0 are discarded on the first start of 1.1.

## Notes

- Throughput is limited by the ESP8266 (single core, 2.4 GHz only). The NAT table holds 512 entries.
- Scanning briefly interrupts the access point.
- Settings live in flash (emulated EEPROM). Display and LED values from the device menu are saved when you leave that screen.
