// Production firmware: no IR receiver. Holds AC state in memory, mirrors it
// to Home Assistant over MQTT (discovery + command/state topics), and sends
// IR codes via IRToshibaAC whenever HA issues a command. MQTT runs over TLS to
// the broker on the VPS (mqtt.tijel.id:8883).
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_bt.h>
#include <ir_Toshiba.h>

#include "secrets.h"

const uint16_t kIrLedPin = 4;
const char *kDeviceId = "toshiba_ac";
const char *kMqttClientId = "toshiba_ac_esp32";

const char *kDiscoveryTopic = "homeassistant/climate/toshiba_ac/config";
const char *kFilterDiscoveryTopic = "homeassistant/switch/toshiba_ac_filter/config";
const char *kModeCommandTopic = "toshiba_ac/mode/set";
const char *kModeStateTopic = "toshiba_ac/mode/state";
const char *kTempCommandTopic = "toshiba_ac/temp/set";
const char *kTempStateTopic = "toshiba_ac/temp/state";
const char *kFanCommandTopic = "toshiba_ac/fan/set";
const char *kFanStateTopic = "toshiba_ac/fan/state";
const char *kSwingCommandTopic = "toshiba_ac/swing/set";
const char *kSwingStateTopic = "toshiba_ac/swing/state";
const char *kPresetCommandTopic = "toshiba_ac/preset/set";
const char *kPresetStateTopic = "toshiba_ac/preset/state";
const char *kFilterCommandTopic = "toshiba_ac/filter/set";
const char *kFilterStateTopic = "toshiba_ac/filter/state";
const char *kAvailabilityTopic = "toshiba_ac/status";

const unsigned long kMqttRetryIntervalMs = 5000;

// Broker cert is Let's Encrypt (leaf -> YR2 -> Root YR, cross-signed by ISRG
// Root X1). Trust both roots so it keeps verifying once the X1 cross-sign is
// dropped. Renewals don't change this unless Let's Encrypt switches roots.
const char kMqttCaCerts[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n"
"-----BEGIN CERTIFICATE-----\n"
"MIIFKTCCAxGgAwIBAgIRAOxGNJNgz0sP+KmC2Tqpyj0wDQYJKoZIhvcNAQELBQAw\n"
"LjELMAkGA1UEBhMCVVMxDTALBgNVBAoTBElTUkcxEDAOBgNVBAMTB1Jvb3QgWVIw\n"
"HhcNMjUwOTAzMDAwMDAwWhcNNDUwOTAyMjM1OTU5WjAuMQswCQYDVQQGEwJVUzEN\n"
"MAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZUjCCAiIwDQYJKoZIhvcNAQEB\n"
"BQADggIPADCCAgoCggIBANvGJnN78CTJdWL3+eGfsLN5TrNBJs+VH9hRXqRbwxu9\n"
"sGNiB0BD1fcOxbSUQCJIM1xE13Db+5Cw1w0s0EBYsvuIP/6joF0w8cuImbgR1OGg\n"
"YbSQ4OpzI+DG8SGuTlcE873OCS+kh3srlo6vl43M5OJg4Aeo1sfHp6kTJDoIiFBN\n"
"JAY+OKfX/FUvYKuhjT+no49lmqmupSBI5PkBQiqrEGtWU5uxU/cQWHGu8jSjFBzn\n"
"ZqvbNPLMXMLFxCb3WTfrJBXXjqvWG+v4bjzxjjeAtOlU7qarRDvNOyAuQYLln904\n"
"M+faKx8hnLCpJ15ZqaEgcNlY+9MMWcC5yvL2A2j3l9+2buggZX+dOE91zYmIdawT\n"
"vSZuVvlbRrAlLxIB6pwMBjneXCjYQ8+3BCCjssbSNpZU3hTcBDdhfAlEDlYr6pEa\n"
"tnMdmDT5BqnKC92bd0EhM1fbLHioLccLCuievT8ZkPhZrq7Mii7gNXAcUEAR8+lz\n"
"Yal+9zTg7C5DALyVOeG/CqfRAMn1KSHCR0NSA6P8tn/mGRlnCct5rtVCLnVySVpU\n"
"6H1qGg3DgTOuskf8eahTMiYbI5ezPJmO5ertalskQ1utp74+eDy92PI4ftHKTbq9\n"
"IWhH4YZKh3WnJEIt+oQvlYZbY8tpEroKrFB6PFGzrJIDRyts4HqvuH52RFj2zv/B\n"
"AgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1Ud\n"
"DgQWBBTe51tg0CJtQCh9Pw0B/qS1UrRRlDANBgkqhkiG9w0BAQsFAAOCAgEAWHnf\n"
"713Bdkq7t5yN2dNIgQakUb94X9WuyhMEHHkgx4oDpSUlnG0w4g94MoqaEUE31ZjR\n"
"LU7L5LD1g9ujFHTQu8AD215AHMVQFbm6j8hQxdXHAzDajFNQnOlDJrLjzIx176oy\n"
"AjvUtejZx2NNmdb5fd0WGVGsCdoAJ3N8ozo7ajE8t6vfxStZb4BQ9WYJGHUDrv2N\n"
"i5tJF6CNiPnlzs3BUfECRbE4JSk+jvy8+VoGiFE8qsH/j78x2fjgQhAQFV7P7Zxy\n"
"dBTZ1wEkNpZNW2qnaK1SKBLa+xf6E06YRIq5uaI+HWH8SY1y5VbRgzq40EKg3yxP\n"
"06fz+uYAUIFJoLNfhwRCc3Q6pQVuMX3yAjHAes4gk4moGcLQ5p7HAh39yeylZc1J\n"
"41sx/jKwLIkPE6Rr1Nf4pxdsxf9SA4yOEiAkDgq04DVxn8hgYFdUtBCuiuVC2heA\n"
"EiqVEa+8QZjuw8Gj0EbHXcRd1nInvGqRS1o9Is7YBdQN57X1AYveGBNNqjICSb7c\n"
"awuw1EawTDrs13VUlJVEsbQ0/O/1aaV73mCdOQ8azqL2KTv1Ewu1xbquE2S+kdQU\n"
"To9TUwat3wUA6cwXh1EfpS/3fJ0aGah5hdpRyoCLDlsSn8tkrjMfFFX0viC+GxHc\n"
"sI1ANRYvqSFC2X1VRZfDg+wD6E21BccmifG4yWc=\n"
"-----END CERTIFICATE-----\n";

IRToshibaAC ac(kIrLedPin);
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

String currentMode = "off";
uint8_t currentTemp = 24;
String currentFan = "auto";
String currentSwing = "off";
String currentPreset = "none";
String currentFilter = "OFF";

unsigned long lastMqttAttemptMs = 0;

const unsigned long kWifiRetryIntervalMs = 10000;
unsigned long lastWifiAttemptMs = 0;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWifiAttemptMs < kWifiRetryIntervalMs) return;
  lastWifiAttemptMs = now;
  Serial.println("WiFi: connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void publishDiscovery() {
  JsonDocument doc;
  doc["name"] = "Toshiba AC";
  doc["unique_id"] = kDeviceId;

  JsonArray modes = doc["modes"].to<JsonArray>();
  modes.add("off");
  modes.add("cool");
  modes.add("heat");
  modes.add("dry");
  modes.add("fan_only");
  modes.add("auto");
  doc["mode_command_topic"] = kModeCommandTopic;
  doc["mode_state_topic"] = kModeStateTopic;

  doc["temperature_command_topic"] = kTempCommandTopic;
  doc["temperature_state_topic"] = kTempStateTopic;
  doc["min_temp"] = kToshibaAcMinTemp;
  doc["max_temp"] = kToshibaAcMaxTemp;
  doc["temp_step"] = 1;

  JsonArray fanModes = doc["fan_modes"].to<JsonArray>();
  fanModes.add("auto");
  fanModes.add("1");
  fanModes.add("2");
  fanModes.add("3");
  fanModes.add("4");
  fanModes.add("5");
  doc["fan_mode_command_topic"] = kFanCommandTopic;
  doc["fan_mode_state_topic"] = kFanStateTopic;

  JsonArray swingModes = doc["swing_modes"].to<JsonArray>();
  swingModes.add("off");
  swingModes.add("on");
  doc["swing_mode_command_topic"] = kSwingCommandTopic;
  doc["swing_mode_state_topic"] = kSwingStateTopic;

  JsonArray presetModes = doc["preset_modes"].to<JsonArray>();
  presetModes.add("boost");
  presetModes.add("eco");
  doc["preset_mode_command_topic"] = kPresetCommandTopic;
  doc["preset_mode_state_topic"] = kPresetStateTopic;

  doc["availability_topic"] = kAvailabilityTopic;
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";

  JsonObject device = doc["device"].to<JsonObject>();
  JsonArray identifiers = device["identifiers"].to<JsonArray>();
  identifiers.add(kMqttClientId);
  device["name"] = "Toshiba AC";
  device["manufacturer"] = "Toshiba";
  device["model"] = "AC";

  char buffer[1536];
  size_t n = serializeJson(doc, buffer);
  if (mqttClient.publish(kDiscoveryTopic, (uint8_t *)buffer, n, true)) {
    Serial.printf("MQTT: discovery published (%d bytes)\n", n);
  } else {
    Serial.printf("MQTT: discovery publish failed (%d bytes, buffer=%d)\n", n,
                  mqttClient.getBufferSize());
  }
}

void publishFilterDiscovery() {
  JsonDocument doc;
  doc["name"] = "Toshiba AC Filter";
  doc["unique_id"] = "toshiba_ac_filter";
  doc["command_topic"] = kFilterCommandTopic;
  doc["state_topic"] = kFilterStateTopic;
  doc["payload_on"] = "ON";
  doc["payload_off"] = "OFF";
  doc["availability_topic"] = kAvailabilityTopic;
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";

  JsonObject device = doc["device"].to<JsonObject>();
  JsonArray identifiers = device["identifiers"].to<JsonArray>();
  identifiers.add(kMqttClientId);
  device["name"] = "Toshiba AC";
  device["manufacturer"] = "Toshiba";
  device["model"] = "AC";

  char buffer[512];
  size_t n = serializeJson(doc, buffer);
  if (!mqttClient.publish(kFilterDiscoveryTopic, (uint8_t *)buffer, n, true)) {
    Serial.printf("MQTT: filter discovery publish failed (%d bytes)\n", n);
  }
}

void publishState() {
  mqttClient.publish(kModeStateTopic, currentMode.c_str(), true);
  mqttClient.publish(kTempStateTopic, String(currentTemp).c_str(), true);
  mqttClient.publish(kFanStateTopic, currentFan.c_str(), true);
  mqttClient.publish(kSwingStateTopic, currentSwing.c_str(), true);
  mqttClient.publish(kPresetStateTopic, currentPreset.c_str(), true);
  mqttClient.publish(kFilterStateTopic, currentFilter.c_str(), true);
}

void handleModeCommand(const String &payload) {
  if (payload == "off") {
    ac.setPower(false);
  } else {
    uint8_t mode;
    if (payload == "cool") {
      mode = kToshibaAcCool;
    } else if (payload == "heat") {
      mode = kToshibaAcHeat;
    } else if (payload == "dry") {
      mode = kToshibaAcDry;
    } else if (payload == "fan_only") {
      mode = kToshibaAcFan;
    } else if (payload == "auto") {
      mode = kToshibaAcAuto;
    } else {
      Serial.printf("CMD: mode '%s' unrecognized, ignoring\n", payload.c_str());
      return;  // unknown mode, ignore
    }
    ac.setPower(true);
    ac.setMode(mode);
  }
  currentMode = payload;
  Serial.printf("IR: sending mode=%s\n", payload.c_str());
  ac.send();
  Serial.println("IR: send() returned");
  publishState();
}

void handleTempCommand(const String &payload) {
  int requested = payload.toInt();
  if (requested < kToshibaAcMinTemp) requested = kToshibaAcMinTemp;
  if (requested > kToshibaAcMaxTemp) requested = kToshibaAcMaxTemp;
  currentTemp = (uint8_t)requested;
  ac.setTemp(currentTemp);
  Serial.printf("IR: sending temp=%d\n", currentTemp);
  ac.send();
  Serial.println("IR: send() returned");
  publishState();
}

void handleFanCommand(const String &payload) {
  uint8_t fan;
  if (payload == "auto") {
    fan = kToshibaAcFanAuto;
  } else if (payload == "1" || payload == "2" || payload == "3" ||
             payload == "4" || payload == "5") {
    fan = payload.toInt();
  } else {
    Serial.printf("CMD: fan '%s' unrecognized, ignoring\n", payload.c_str());
    return;  // unknown fan mode, ignore
  }
  ac.setFan(fan);
  currentFan = payload;
  Serial.printf("IR: sending fan=%s\n", payload.c_str());
  ac.send();
  Serial.println("IR: send() returned");
  publishState();
}

void handleSwingCommand(const String &payload) {
  if (payload == "on") {
    ac.setSwing(kToshibaAcSwingOn);
  } else if (payload == "off") {
    ac.setSwing(kToshibaAcSwingOff);
  } else {
    Serial.printf("CMD: swing '%s' unrecognized, ignoring\n", payload.c_str());
    return;
  }
  currentSwing = payload;
  Serial.printf("IR: sending swing=%s\n", payload.c_str());
  ac.send();
  Serial.println("IR: send() returned");
  publishState();
}

void handlePresetCommand(const String &payload) {
  if (payload == "none") {
    ac.setTurbo(false);
    ac.setEcono(false);
  } else if (payload == "boost") {
    ac.setEcono(false);
    ac.setTurbo(true);
  } else if (payload == "eco") {
    ac.setTurbo(false);
    ac.setEcono(true);
  } else {
    Serial.printf("CMD: preset '%s' unrecognized, ignoring\n", payload.c_str());
    return;
  }
  currentPreset = payload;
  Serial.printf("IR: sending preset=%s\n", payload.c_str());
  ac.send();
  Serial.println("IR: send() returned");
  publishState();
}

void handleFilterCommand(const String &payload) {
  if (payload != "ON" && payload != "OFF") {
    Serial.printf("CMD: filter '%s' unrecognized, ignoring\n", payload.c_str());
    return;
  }
  ac.setFilter(payload == "ON");
  currentFilter = payload;
  Serial.printf("IR: sending filter=%s\n", payload.c_str());
  ac.send();
  Serial.println("IR: send() returned");
  publishState();
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String payloadStr;
  payloadStr.reserve(length);
  for (unsigned int i = 0; i < length; i++) payloadStr += (char)payload[i];

  Serial.printf("MQTT: rx topic=%s payload=%s\n", topic, payloadStr.c_str());

  String topicStr(topic);
  if (topicStr == kModeCommandTopic) {
    handleModeCommand(payloadStr);
  } else if (topicStr == kTempCommandTopic) {
    handleTempCommand(payloadStr);
  } else if (topicStr == kFanCommandTopic) {
    handleFanCommand(payloadStr);
  } else if (topicStr == kSwingCommandTopic) {
    handleSwingCommand(payloadStr);
  } else if (topicStr == kPresetCommandTopic) {
    handlePresetCommand(payloadStr);
  } else if (topicStr == kFilterCommandTopic) {
    handleFilterCommand(payloadStr);
  } else {
    Serial.println("MQTT: topic matched no handler");
  }
}

void connectMqtt() {
  if (mqttClient.connected()) return;
  unsigned long now = millis();
  if (now - lastMqttAttemptMs < kMqttRetryIntervalMs) return;
  lastMqttAttemptMs = now;

  Serial.println("MQTT: connecting...");
  if (mqttClient.connect(kMqttClientId, MQTT_USERNAME, MQTT_PASSWORD,
                          kAvailabilityTopic, 1, true, "offline")) {
    Serial.println("MQTT: connected");
    mqttClient.publish(kAvailabilityTopic, "online", true);
    publishDiscovery();
    publishFilterDiscovery();
    publishState();
    mqttClient.subscribe(kModeCommandTopic);
    mqttClient.subscribe(kTempCommandTopic);
    mqttClient.subscribe(kFanCommandTopic);
    mqttClient.subscribe(kSwingCommandTopic);
    mqttClient.subscribe(kPresetCommandTopic);
    mqttClient.subscribe(kFilterCommandTopic);
  } else {
    Serial.printf("MQTT: connect failed, rc=%d\n", mqttClient.state());
  }
}

void setup() {
  // Thermal: 240MHz buys nothing here — MQTT + IR are idle-dominated. 80MHz is
  // the floor for WiFi (APB must stay >= 80MHz). Must precede Serial.begin() or
  // the UART divisor is computed against the old clock and output garbles.
  setCpuFrequencyMhz(80);
  btStop();  // BT radio never used

  Serial.begin(115200);
  Serial.println("production: booting");
  ac.begin();
  wifiClient.setCACert(kMqttCaCerts);
  mqttClient.setBufferSize(1536);

#ifdef WIFI_DEBUG_SCAN
  Serial.println("WiFi: scanning...");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    Serial.printf("  [%d] %s (rssi=%d)\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }
  WiFi.scanDelete();
#endif

  connectWiFi();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  static bool wasConnected = false;
  connectWiFi();
  bool isConnected = WiFi.status() == WL_CONNECTED;
  if (!isConnected) {
    static wl_status_t lastStatus = (wl_status_t)255;
    wl_status_t status = WiFi.status();
    if (status != lastStatus) {
      Serial.printf("WiFi: status=%d\n", status);
      lastStatus = status;
    }
  }
  if (isConnected && !wasConnected) {
    Serial.print("WiFi: connected, IP=");
    Serial.println(WiFi.localIP());
  }
  wasConnected = isConnected;
  if (isConnected) {
    connectMqtt();
    mqttClient.loop();
  }
  // Yields to the idle task so the CPU parks between polls. 50ms is
  // imperceptible for AC control and still drains a burst of retained command
  // topics quickly (PubSubClient handles one packet per loop() call).
  delay(50);
}
