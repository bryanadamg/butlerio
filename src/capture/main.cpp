// Capture env: point the real Toshiba remote at the IR receiver and press
// buttons. Confirms the decoded protocol/state length before the production
// env trusts sending synthesized codes.
#include <Arduino.h>
#include <IRrecv.h>
#include <IRtext.h>
#include <IRutils.h>
#include <ir_Toshiba.h>

const uint16_t kIrReceiverPin = 14;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kIrTimeoutMs = 15;

IRrecv irrecv(kIrReceiverPin, kCaptureBufferSize, kIrTimeoutMs, true);
decode_results results;

void setup() {
  Serial.begin(115200);
  delay(500);
  irrecv.enableIRIn();
  Serial.println(F("IR capture ready. Point the Toshiba remote at the receiver and press a button."));
}

void loop() {
  if (irrecv.decode(&results)) {
    Serial.println(F("----"));
    Serial.print(F("Protocol: "));
    Serial.println(typeToString(results.decode_type, results.repeat));
    Serial.print(F("State length (bytes): "));
    Serial.println(results.bits / 8);
    Serial.println(resultToHumanReadableBasic(&results));

    if (results.decode_type == decode_type_t::TOSHIBA_AC) {
      IRToshibaAC ac(0);
      ac.setRaw(results.state, results.bits / 8);
      Serial.println(F("Decoded as TOSHIBA_AC:"));
      Serial.println(ac.toString());
    } else {
      Serial.println(F("WARNING: not decoded as TOSHIBA_AC. Check protocol name above."));
    }
    irrecv.resume();
  }
}
