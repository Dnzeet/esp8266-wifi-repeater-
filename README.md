# ESP8266 Wi-Fi Repeater (Wemos D1 mini and D1 mini Pro)

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

The battery is read on A0 through your voltage divider. The D1 mini and D1 mini Pro already have a 220k/100k divider on A0, so an extra series resistor from the battery is usually all that is added.

1. Flash and open the web UI, Device > Battery.
2. Measure the battery with a multimeter, type the value into "Calibrate" and press the button.

Calibration sets the "voltage that reads as full scale" value, so it accounts for your exact resistors. The default (7400 mV) matches two 100k resistors as a divider (100k from battery plus to A0, 100k from A0 to ground) in front of the onboard divider. Other common setups:

| Setup in front of the board | Full-scale value |
|---|---|
| 100k from battery plus to A0 (series only) | 4200 mV |
| 100k + 100k divider, midpoint to A0 | 7400 mV |

The ADC full scale is nominally 1.0 V but varies a little per chip, so one Calibrate with a multimeter is still worth doing. A 100 nF capacitor from A0 to ground steadies the reading when the divider has a high resistance.

Percent comes from a typical single-cell Li-ion discharge curve. Below 2.5 V (for example when running from USB with no battery) the screen shows USB instead of a battery. The firmware cannot tell if the battery is charging.

## Saved networks

Every network you join is remembered with its password (up to 8). After a restart, or when the link is lost, the repeater scans and reconnects to the strongest saved network in range, so moving between places needs no typing.

- On the device: Menu > Saved shows the list. OK on an entry offers Join or Forget. In Networks, saved entries show a star and connect directly.
- In the web UI: Connect tab, Saved networks, with Join and Forget buttons.
- A saved network with a hidden name cannot be found by scanning, so join it manually when needed.
- While nobody is connected to the repeater's own network, retries scan every 30 to 45 seconds. Scans pause while a device is attached, so the setup page is not disturbed.

## Internet status and speed

- The device checks the internet every 30 seconds (10 seconds while it looks down): a TCP connection to 1.1.1.1 (8.8.8.8 as fallback) gives the latency, and a DNS query to the selected DNS server checks name resolution. The result appears on the home screen (globe with latency, a cross, or DNS!), in Menu > Info, in the web UI Overview and in the LED (orange when connected without internet). The uplink label changes to NO NET.
- Speed test: Menu > Speed test or the button in the web UI. It downloads up to about 6 MB over plain HTTP (cachefly, speedtest.tele2.net, proof.ovh.net, then thinkbroadband, whichever answers first; plain HTTP redirects are followed, HTTPS-only servers are skipped; a failure shows the reason, for example "HTTP 301") and reports Mbps. This measures the ESP8266's own download speed, so it indicates what the repeater can reach. While it runs, the device is busy for a few seconds. The last result alternates with the latency on the home screen.
- Probes block the device for up to a couple of seconds when the internet is down. They are skipped while you are typing.

## DNS

Choose the DNS that connected devices receive: same as upstream (default), Cloudflare, Google, Quad9, AdGuard (blocks ads and trackers) or a custom address. Set it in the web UI (Access point tab) or cycle it from Menu > DNS with Left and Right. The core used here hands out a single DNS address. Devices pick up a change when they reconnect or renew their address.

## Virtual buttons

The web UI Overview has a remote under the live copy of the screen: Up, Down, Left, Right, OK, Back and Home (same as holding Back). They work exactly like the hardware buttons, so the whole menu can be driven from the browser. Holding a direction repeats it. On a computer the arrow keys, Enter, Backspace (Back) and Escape (Home) work too.

## Traffic meter

The home screen shows live download and upload under the ping, in the top-right corner (down arrow and up arrow, in K or M, kilobits or megabits per second). It counts the traffic of devices connected to the repeater, so it behaves like the speed meter on a phone. The web UI shows the same under Overview > Traffic.

## Status LED

Pick what the NeoPixel shows in the web UI or on the device (Menu > LED):

| Mode        | Behaviour |
|-------------|-----------|
| Off         | LED dark |
| Connection  | Green: connected and repeating. Blue: connected, repeater off. Amber pulse: connecting. Red pulse: failed or link lost. Violet pulse: no network saved yet |
| Battery     | Green 60% and up, amber 30-59%, orange 15-29%, red flashing below 15% |
| Alternate   | Connection colour for three seconds, battery colour for one |
| Ping        | Green under 50 ms, lime under 100, amber under 200, orange above. Red pulse without internet, violet for a DNS problem |

Onboard LED (GPIO2): Menu > LED > Onboard ping, or the web UI. One blink under 50 ms, two under 120 ms, three when slower, one long blink for a DNS problem, rapid flicker without internet. GPIO2 is also the default OK button, so a press during a blink can be missed.

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

Menu: Networks (scan, pick, type password), Saved (join or forget remembered networks), Repeater (on/off), DNS (cycle preset), Display (brightness, timeout, flip), LED, Speed test, Info, Restart.

Keyboard keys: `abc` / `ABC` / `123` / `#+=` switch the character page, `SPC` space, `DEL` delete, `SHOW` / `HIDE` reveal the password, `OK` connects (minimum 8 characters).

## Web UI

1. Join the repeater's own network. Defaults: name `ESP-Repeater`. It is open (no password) while the repeater has no upstream connection, so first setup needs no password. After it connects, the access point switches to its password (default `repeater123`). Name, password and this behaviour are set under Access point.
2. Open `http://192.168.4.1` (also shown under Menu > Info).
3. Overview: live copy of the OLED, signal / battery / client readouts, 3-minute signal history, connected devices. Connect, Access point and Device tabs hold the settings.

The web UI only answers clients on the repeater's own network, not devices on the upstream Wi-Fi.
Change the default access point password first.

If the upstream network also uses 192.168.4.x, the repeater moves its own network to 192.168.45.x automatically.

## Build

GitHub: push this folder to a repository. `.github/workflows/build.yml` builds on every push and uploads `wifi-repeater-d1-mini-4mb.bin` as an artifact. A tag such as `v1.1.0` also attaches it to a release.

Local: `pio run`.

## Flash

```
esptool.py --chip esp8266 --baud 460800 write_flash 0x0 wifi-repeater-d1-mini-4mb.bin
```

The image is built for 4 MB of flash, so it runs on the regular D1 mini (4 MB) and on the D1 mini Pro (16 MB, the rest stays unused). Run `esptool.py erase_flash` once first. Settings from earlier versions are discarded on the first start of this one (the storage format changed).

## Notes

- Throughput is limited by the ESP8266 (single core, 2.4 GHz only). The NAT table holds 512 entries.
- Scanning briefly interrupts the access point.
- Settings live in flash (emulated EEPROM). Display and LED values from the device menu are saved when you leave that screen.
