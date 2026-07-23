# deploy

Home Assistant + Mosquitto, as Docker containers alongside your Pi's other services. Run from this directory on the Pi.

## First-time setup

1. Generate the Mosquitto password file (gitignored, not committed):

   ```
   docker run --rm -it -v "$(pwd)/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 mosquitto_passwd -c /mosquitto/config/passwd toshiba_ac
   ```

2. Start both containers:

   ```
   docker compose up -d
   ```

3. Onboard Home Assistant at `http://<pi-ip>:8123`, create admin account.

4. Add the broker: Settings -> Devices & Services -> Add Integration -> MQTT -> host `localhost`, port `1883`, username `toshiba_ac`, the password from step 1.

5. Fill `../src/production/secrets.h`:
   - `MQTT_HOST` = Pi's LAN IP
   - `MQTT_USERNAME` = `toshiba_ac`
   - `MQTT_PASSWORD` = the password from step 1

## Notes

- `homeassistant` runs with `network_mode: host` — needed for mDNS/local discovery. `mosquitto` doesn't need it, port `1883` is published directly.
- HA here is the **Container** install (no Supervisor) — no add-on store. Mosquitto runs as its own container instead of an HA add-on.
