// Production firmware: no IR receiver. Holds AC state in memory, mirrors it
// to Home Assistant over MQTT (discovery + command/state topics), and sends
// IR codes via IRToshibaAC whenever HA issues a command.
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
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

IRToshibaAC ac(kIrLedPin);
WiFiClient wifiClient;
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
  presetModes.add("none");
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
  if (!mqttClient.publish(kDiscoveryTopic, (uint8_t *)buffer, n, true)) {
    Serial.printf("MQTT: discovery publish failed (%d bytes)\n", n);
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
  Serial.begin(115200);
  Serial.println("production: booting");
  ac.begin();
  mqttClient.setBufferSize(1536);

  Serial.println("WiFi: scanning...");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    Serial.printf("  [%d] %s (rssi=%d)\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }
  WiFi.scanDelete();

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
}
