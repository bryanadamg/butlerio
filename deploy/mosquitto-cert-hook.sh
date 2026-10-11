#!/bin/sh
# certbot deploy hook: copy the renewed mqtt cert where the mosquitto container
# can read it (runs as uid 1883), then restart it to load the new cert.
set -e
domain=mqtt.tijel.id
dest="$(dirname "$(readlink -f "$0")")/mosquitto/config/certs"
mkdir -p "$dest"
cp -L "/etc/letsencrypt/live/$domain/fullchain.pem" "/etc/letsencrypt/live/$domain/privkey.pem" "$dest/"
chown -R 1883:1883 "$dest"
chmod 600 "$dest/privkey.pem"
docker restart mosquitto >/dev/null 2>&1 || true
