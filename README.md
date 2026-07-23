# butlerio

## Capture findings (2026-07-23)

Confirmed on real Toshiba remote via `capture` env:

- Protocol: `TOSHIBA_AC`
- State length: 9 bytes (72 bits)
- Model: `0` (TOSHIBA REMOTE A)
- Decoded correctly across power/mode/temp/fan changes, e.g.:
  - `0xF20D03FC0130610050` → Cool, 20C, Fan UNKNOWN(2)
  - `0xF20D03FC0120010020` → Cool, 19C, Fan Auto(0)
  - `0xF20D03FC0120410060` → Cool, 19C, Fan Low(1)
  - `0xF20D03FC01208100A0` → Cool, 19C, Fan Medium(3)

Interleaved `UNKNOWN`/148-bit/18-byte captures on some presses are a separate frame (checksum/repeat or another button) IRremoteESP8266 has no decoder for — not real noise, but not needed since the 9-byte `TOSHIBA_AC` frame decodes correctly and is what `IRToshibaAC` in the production env sends.

`production` env's use of `IRToshibaAC` (9-byte state) is confirmed compatible with this AC.