#include "mqtt_manager.h"
#include "config.h"

#include <WiFiClientSecure.h>
#include <PubSubClient.h>

namespace mqtt{
  static WiFiClientSecure s_tlsClient;
  static PubSubClient     s_mqtt(s_tlsClient);

  static const char* s_user = nullptr;
  static const char* s_pass = nullptr;

  static MqttMessageHandler s_userHandler = nullptr;

  static unsigned long s_lastReconnectAttempt = 0;

  static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    String msg;
    msg.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
      msg += (char)payload[i];
    }

    if (s_userHandler != nullptr) {
      s_userHandler(topic, msg);
    } else {
      Serial.printf("[MQTT] RX  %s : %s\n", topic, msg.c_str());
    }
  }

  static String makeClientId() {
    return String(MQTT_CLIENT_PREFIX) + String((uint32_t)ESP.getEfuseMac(), HEX);
  }

  static bool connectOnce() {
    String clientId = makeClientId();
    Serial.printf("[MQTT] Verbinde als '%s' ...\n", clientId.c_str());

    bool ok = s_mqtt.connect(clientId.c_str(), s_user, s_pass);
    if (ok) {
      Serial.println("[MQTT] verbunden.");
      s_mqtt.subscribe(SUB_SPIEL);
      Serial.printf("[MQTT] subscribed: %s\n", SUB_SPIEL);
      s_mqtt.subscribe(SUB_START);
      Serial.printf("[MQTT] subscribed: %s\n", SUB_START);
      s_mqtt.subscribe(SUB_SPIELER);
      Serial.printf("[MQTT] subscribed: %s\n", SUB_SPIELER);
      s_mqtt.subscribe(SUB_KEKSE);
      Serial.printf("[MQTT] subscribed: %s\n", SUB_KEKSE);
      s_mqtt.subscribe(SUB_WAND);
      Serial.printf("[MQTT] subscribed: %s\n", SUB_WAND);
    } else {
      Serial.printf("[MQTT] fehlgeschlagen, state=%d\n", s_mqtt.state());
    }
    return ok;
  }

  void mqttSetup(const char* server, uint16_t port,
                const char* user, const char* pass,
                const char* rootCA) {
    s_user = user;
    s_pass = pass;

    s_tlsClient.setCACert(rootCA);

    s_mqtt.setServer(server, port);
    s_mqtt.setCallback(onMqttMessage);
    s_mqtt.setBufferSize(512);

    connectOnce();
  }

  void mqttLoop() {
    if (!s_mqtt.connected()) {
      unsigned long now = millis();
      if (now - s_lastReconnectAttempt >= MQTT_RECONNECT_INTERVAL_MS) {
        s_lastReconnectAttempt = now;
        connectOnce();
      }
    } else {
      s_mqtt.loop();
    }
  }

  bool mqttConnected() {
    return s_mqtt.connected();
  }

  bool mqttPublish(const char* topic, const char* payload, bool retained) {
    if (!s_mqtt.connected()) return false;
    return s_mqtt.publish(topic, payload, retained);
  }

  void mqttSetMessageHandler(MqttMessageHandler handler) {
    s_userHandler = handler;
  }
}