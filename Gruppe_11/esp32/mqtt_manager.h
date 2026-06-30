#pragma once
#include <Arduino.h>
#include <wifi_mqtt_secrets.h>

namespace mqtt{
  typedef void (*MqttMessageHandler)(const char* topic, const String& payload);

  void mqttSetup(const char* mqtt_server, uint16_t mqtt_port, const char* mqtt_username, const char* mqtt_password, const char* root_ca);

  void mqttLoop();

  bool mqttConnected();

  bool mqttPublish(const char* topic, const char* payload, bool retained = false);

  void mqttSetMessageHandler(MqttMessageHandler handler);
}