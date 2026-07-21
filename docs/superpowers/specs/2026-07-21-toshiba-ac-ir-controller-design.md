# Toshiba AC IR Controller — Design

## Goal

Control a Toshiba air conditioner from Home Assistant via ESP32 + IR LED, using the AC's decoded IR protocol (not raw blob replay) so HA gets a real climate entity with temp/mode/fan control.

## Architecture

```
[Toshiba AC] <--IR--- [ESP32 + IR LED via transistor]
                            |
                         WiFi/MQTT
                            |
                    [Mosquitto broker]
                            |
                      [Home Assistant]
```

One PlatformIO project, two build environments sharing `platformio.ini` (different `build_src_filter`), same board config (`esp32dev`):

1. **Capture env** (temporary, dev-only) — IR receiver wired in, decodes button presses from the real remote, prints protocol/state bytes over serial. Used once to confirm the exact Toshiba IR variant (state length, bit layout) before trusting the send path.
2. **Production env** — no IR receiver. WiFi + MQTT client, holds AC state in an `IRToshibaAC` object, exposes a Home Assistant MQTT Climate entity via MQTT discovery, translates HA commands into IR sends.

## Hardware

- ESP32 dev board (`esp32dev`)
- IR LED (940nm), driven via NPN transistor (2N2222 or BC547):
  - GPIO4 → base resistor (~1kΩ) → transistor base
  - Transistor emitter → GND
  - Transistor collector → IR LED cathode; IR LED anode → resistor (~150Ω, size to LED datasheet Vf/If) → 5V (VIN)
- IR receiver module (TSOP-style), breadboard only, capture env only:
  - VCC → 3.3V, GND → GND, OUT → GPIO14
  - No external resistor needed (internal demodulator)

Exact LED resistor value pending — user checking IR LED datasheet before final purchase.

## Firmware Components

### Capture env (`src_capture/main.cpp`)

- `IRrecv` on GPIO14.
- Prints decoded protocol name, state bytes, and decoded mode/temp/fan (via `IRToshibaAC` state parsing) over serial.
- Used to validate that captured codes match a `TOSHIBA_AC` decode and to confirm state length (`kToshibaACStateLength` or variant) before trusting the send path.

### Production env (`src/main.cpp`)

- `secrets.h` (gitignored): WiFi SSID/pass, MQTT host/port/user/pass.
- WiFi connect + reconnect handling in main loop (non-blocking).
- `PubSubClient` for MQTT, reconnect with backoff (~5s retry), re-subscribe on reconnect.
- `IRToshibaAC ac(GPIO4)` holds current AC state (power, mode, temp, fan) in memory.
- On boot: publish retained MQTT discovery payload to `homeassistant/climate/toshiba_ac/config` (modes, temp range 17–30°C step 1, fan modes) so HA auto-creates the climate entity.
- Subscribe to command topics; on message, update `ac` state, clamp temp to valid range, call `ac.send()`, then publish updated state back (retained) so HA UI reflects immediately and survives HA restart.
- Publish availability (LWT) so HA marks entity unavailable if ESP32 drops off MQTT.

## MQTT Topics & HA Discovery

Device id: `toshiba_ac` (single unit, fixed).

- Discovery config (retained): `homeassistant/climate/toshiba_ac/config`
- Commands (subscribe): `toshiba_ac/mode/set`, `toshiba_ac/temp/set`, `toshiba_ac/fan/set`
- State (publish, retained): `toshiba_ac/mode/state`, `toshiba_ac/temp/state`, `toshiba_ac/fan/state`
- Availability (publish, retained, LWT): `toshiba_ac/status` → `online`/`offline`

## Credentials

WiFi/MQTT credentials live in a gitignored `secrets.h` header (SSID/pass, broker host/port/user/pass). Simple, no recompile-free WiFi config needed for a single home device.

## Error Handling

- WiFi drop: loop checks `WiFi.status()`, reconnects without blocking MQTT loop.
- MQTT drop: `PubSubClient` reconnect with backoff, re-subscribe on reconnect.
- Bad/out-of-range command payload: clamp or ignore, never crash, re-ack current state.

## Testing

No unit test framework — hardware-in-loop only:

1. Capture env confirms decode matches `TOSHIBA_AC` protocol and expected state length.
2. Production env tested by sending MQTT commands (HA UI or `mosquitto_pub`) and observing the physical AC response.

## Out of Scope

- Mosquitto broker install/config on Home Assistant (prerequisite, done separately, not part of this firmware repo).
- Syncing state from manual remote use (IR receiver not present in production build — HA state can drift from actual AC state if the physical remote is used directly).
- WiFiManager captive portal / runtime WiFi reconfiguration.
