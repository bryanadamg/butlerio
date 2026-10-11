# deploy

Home Assistant + Mosquitto + Cloudflare tunnel, as Docker containers on the VPS (`tijel-vps`, cloned at `/opt/butlerio`). Run from this directory.

- HA is reached only through the tunnel (`home.tijel.id` → `http://homeassistant:8123`); no port is published.
- Mosquitto publishes only `8883` (TLS, `mqtt.tijel.id`) for the ESP32. HA talks to it over the compose network on plaintext `mosquitto:1883`.
- `homeassistant/`, `cloudflared/`, `mosquitto/data/`, `mosquitto/config/passwd` and `mosquitto/config/certs/` are gitignored state — back them up / copy them when moving hosts.

## First-time setup

1. DNS: `mqtt` A record → VPS IP, **DNS only** (Cloudflare's proxy can't carry MQTT).

2. TLS cert (port 80 must be reachable; certbot's timer handles renewal and re-runs the hook):

   ```
   certbot certonly --standalone -d mqtt.tijel.id --key-type rsa --deploy-hook "$(pwd)/mosquitto-cert-hook.sh"
   ```

3. Generate the Mosquitto password file:

   ```
   docker run --rm -it -v "$(pwd)/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 mosquitto_passwd -c /mosquitto/config/passwd toshiba_ac
   ```

4. Start everything:

   ```
   docker compose up -d
   ```

5. Onboard Home Assistant at `https://home.tijel.id`, then add the broker: Settings -> Devices & Services -> Add Integration -> MQTT -> host `mosquitto`, port `1883`, username `toshiba_ac`, the password from step 3.

6. Fill `../src/production/secrets.h`: `MQTT_HOST` = `mqtt.tijel.id`, `MQTT_PORT` = `8883`, `MQTT_USERNAME`/`MQTT_PASSWORD` from step 3.

## Notes

- HA here is the **Container** install (no Supervisor) — no add-on store.
- The VPS has ~1GB RAM; HA leans on the 2GB swap file during startup/updates.
- The firmware trusts ISRG Root X1 and Root YR (`kMqttCaCerts` in `src/production/main.cpp`). Renewals don't need a reflash unless Let's Encrypt moves the cert to a different root.
