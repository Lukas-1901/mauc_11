#include "config.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include <time.h>

unsigned long lastTestPublish = 0;

// NTP-Zeitsync. PFLICHT vor TLS: WiFiClientSecure prueft das Gueltigkeitsdatum
// des Broker-Zertifikats. Ohne gesetzte Uhr steht der ESP auf 1970 -> Cert
// "noch nicht gueltig" -> Handshake schlaegt fehl (state=-2).
static void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");   // UTC
  Serial.print("[NTP] Zeit synchronisieren");
  time_t now = time(nullptr);
  while (now < 1700000000) {            // < ~2023 => noch nicht gesetzt
    delay(200);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.printf("\n[NTP] gesetzt: %ld\n", (long)now);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  wifimgr::WifiBegin();
  syncTime();
  mqttSetup(mqtt_server, mqtt_port, mqtt_username, mqtt_password, root_ca);
}

void loop() {
  wifimgr::WifiLoop();
  mqttLoop();

  unsigned long now = millis();
  if (mqttConnected() && now - lastTestPublish >= TEST_PUBLISH_INTERVAL_MS) {
    lastTestPublish = now;
    char buf[48];
    snprintf(buf, sizeof(buf), "hello %ld", (long)time(nullptr));  // echter Timestamp
    if (mqttPublish(TOPIC_TEST_PUB, buf)) {
      Serial.printf("[MQTT] TX  %s : %s\n", TOPIC_TEST_PUB, buf);
    }
  }
}