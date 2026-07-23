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
const char *kModeCommandTopic = "toshiba_ac/mode/set";
const char *kModeStateTopic = "toshiba_ac/mode/state";
const char *kTempCommandTopic = "toshiba_ac/temp/set";
const char *kTempStateTopic = "toshiba_ac/temp/state";
const char *kFanCommandTopic = "toshiba_ac/fan/set";
const char *kFanStateTopic = "toshiba_ac/fan/state";
const char *kAvailabilityTopic = "toshiba_ac/status";

const unsigned long kMqttRetryIntervalMs = 5000;

IRToshibaAC ac(kIrLedPin);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

String currentMode = "off";
uint8_t currentTemp = 24;
String currentFan = "auto";

unsigned long lastMqttAttemptMs = 0;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
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
  fanModes.add("low");
  fanModes.add("medium");
  fanModes.add("high");
  doc["fan_mode_command_topic"] = kFanCommandTopic;
  doc["fan_mode_state_topic"] = kFanStateTopic;

  doc["availability_topic"] = kAvailabilityTopic;
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";

  JsonObject device = doc["device"].to<JsonObject>();
  JsonArray identifiers = device["identifiers"].to<JsonArray>();
  identifiers.add(kMqttClientId);
  device["name"] = "Toshiba AC";
  device["manufacturer"] = "Toshiba";
  device["model"] = "AC";

  char buffer[1024];
  size_t n = serializeJson(doc, buffer);
  mqttClient.publish(kDiscoveryTopic, (uint8_t *)buffer, n, true);
}

void publishState() {
  mqttClient.publish(kModeStateTopic, currentMode.c_str(), true);
  mqttClient.publish(kTempStateTopic, String(currentTemp).c_str(), true);
  mqttClient.publish(kFanStateTopic, currentFan.c_str(), true);
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
      return;  // unknown mode, ignore
    }
    ac.setPower(true);
    ac.setMode(mode);
  }
  currentMode = payload;
  ac.send();
  publishState();
}

void handleTempCommand(const String &payload) {
  int requested = payload.toInt();
  if (requested < kToshibaAcMinTemp) requested = kToshibaAcMinTemp;
  if (requested > kToshibaAcMaxTemp) requested = kToshibaAcMaxTemp;
  currentTemp = (uint8_t)requested;
  ac.setTemp(currentTemp);
  ac.send();
  publishState();
}

void handleFanCommand(const String &payload) {
  uint8_t fan;
  if (payload == "auto") {
    fan = kToshibaAcFanAuto;
  } else if (payload == "low") {
    fan = kToshibaAcFanMin;
  } else if (payload == "medium") {
    fan = kToshibaAcFanMed;
  } else if (payload == "high") {
    fan = kToshibaAcFanMax;
  } else {
    return;  // unknown fan mode, ignore
  }
  currentFan = payload;
  ac.setFan(fan);
  ac.send();
  publishState();
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String payloadStr;
  payloadStr.reserve(length);
  for (unsigned int i = 0; i < length; i++) payloadStr += (char)payload[i];

  String topicStr(topic);
  if (topicStr == kModeCommandTopic) {
    handleModeCommand(payloadStr);
  } else if (topicStr == kTempCommandTopic) {
    handleTempCommand(payloadStr);
  } else if (topicStr == kFanCommandTopic) {
    handleFanCommand(payloadStr);
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
    publishState();
    mqttClient.subscribe(kModeCommandTopic);
    mqttClient.subscribe(kTempCommandTopic);
    mqttClient.subscribe(kFanCommandTopic);
  } else {
    Serial.printf("MQTT: connect failed, rc=%d\n", mqttClient.state());
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("production: booting");
  ac.begin();
  connectWiFi();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  static bool wasConnected = false;
  connectWiFi();
  bool isConnected = WiFi.status() == WL_CONNECTED;
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
