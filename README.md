# DeskLED

Custom firmware for an RGB desk light driven by a Tuya TYWE3L module (ESP8266EX, 2 MB flash) and three
IRLZ34N low-side MOSFETs on GPIO12 (red), GPIO13 (green) and GPIO14 (blue).

## Features

- Effects: solid, RGB fade (default), breathe, color jump, candle, strobe, with speed, brightness and fade transitions
- Effect scripts: formula-based effects (TinyExpr) edited in the web UI with live preview, stored on the device
- Temporary effects: apply any effect for a duration, then return to the previous state
- REST API and a single-page web UI
- Wi-Fi setup through a captive-portal hotspot, or over serial; optional static IP
- Wi-Fi console over telnet (port 23) mirroring the serial console
- Signed OTA updates (only images signed with your key are accepted)
- Admin login: session login in the web UI, HTTP Digest for tools; passwords are never stored in plain text
- MQTT with Home Assistant discovery — tested against Mosquitto (discovery, commands, state, last will, broker auth); not yet tried inside Home Assistant itself

## Hardware

Bill of materials, schematic and wiring tables: [HARDWARE.md](HARDWARE.md).

### Notes

- Add 100Ω in series and 10kΩ gate-to-GND on each MOSFET gate, otherwise the LEDs light up while the ESP boots.
- The ESP8266 draws ~300-400 mA spikes when the radio transmits. A weak 3.3V supply causes resets and
  "illegal instruction" crashes; use a supply rated ≥500 mA with a capacitor (100-470 µF) at the module.
- Keep GPIO0 and GPIO2 pulled high and GPIO15 pulled low at boot.

## Build and flash

Requires [PlatformIO](https://platformio.org/).

```sh
cd firmware

# once: create your own signing key pair (replaces the public.key in this repo;
# keep keys/private.key secret and backed up, it is gitignored)
scripts/genkeys.sh

# first flash over serial: hold GPIO0 low and reset the module
pio run -e tywe3l -t upload

# later updates over Wi-Fi (uploads the signed image through /update)
DESKLED_PASSWORD=... pio run -e tywe3l_ota -t upload
```

Set `upload_port` in `platformio.ini` to your device's hostname or IP.

## First setup

1. On first boot the device opens the `DeskLED-xxxx` hotspot (password `deskled-setup`).
2. Join it, pick your network in the web page, and save. Or over serial: `wifi "<ssid>" <password>`.
3. Log in at `http://deskled-xxxx.local/` with the default admin password `deskled-ota` and change it under Security.

Serial and telnet commands: `wifi`, `ip`, `mqtt`, `password`, `scan`, `status`, `reboot`, `help`.

## Home Assistant

Enable MQTT in the web UI (or `mqtt <host> [port] [user] [pass]` over serial) and the device publishes
discovery, so it appears as a light with brightness, RGB, the effect list (built-ins plus your scripts)
and an "Effect speed" number. Topics live under `deskled/<hostname>/`:
`state` (retained), `set`, `speed/set` and `availability` (retained, last will).

`set` takes Home Assistant's JSON light schema, plus two extras: `duration` (ms) runs the change as a
temporary effect, and `flash` maps to a 2 s or 10 s strobe.

```sh
mosquitto_pub -h broker -t deskled/deskled-xxxx/set \
  -m '{"effect":"strobe","color":{"r":255,"g":0,"b":0},"duration":10000}'
```

For Apple Home, add Home Assistant's HomeKit Bridge integration and include the light.

## API

All endpoints except `/`, `/update` (GET) and `/api/login` require login (session cookie or HTTP Digest, user `admin`).

| Endpoint | Description |
|---|---|
| `GET/POST /api/state` | Read or change `on`, `toggle`, `brightness`, `color`, `mode`, `speed`, `transition`; add `duration` (ms) for a temporary change |
| `DELETE /api/state/temporary` | Cancel a temporary effect |
| `GET /api/modes` | Built-in effects and saved scripts |
| `GET/POST/DELETE /api/scripts` | List, save or delete effect scripts |
| `POST /api/scripts/preview` | Run an unsaved script temporarily |
| `GET /api/info` | Device info |
| `GET /api/wifi/scan`, `POST/DELETE /api/wifi` | Scan, save or clear Wi-Fi settings |
| `GET/POST /api/mqtt` | MQTT broker settings |
| `POST /api/password`, `POST /api/ap-password` | Change admin or hotspot password |
| `POST /api/reboot` | Reboot |
| `POST /update` | Upload a signed firmware image |

Example, as a tool with Digest auth:

```sh
curl --digest -u admin:PASSWORD -X POST http://deskled-xxxx.local/api/state \
  -d '{"mode":"strobe","color":"#ff0000","duration":10000}'
```

## License

MIT, see [LICENSE](LICENSE).

## Third-party code

- [TinyExpr](https://github.com/codeplea/tinyexpr) (zlib license), vendored in `firmware/lib/tinyexpr`
- [ArduinoJson](https://arduinojson.org/) and [PubSubClient](https://github.com/knolleary/pubsubclient) via PlatformIO
