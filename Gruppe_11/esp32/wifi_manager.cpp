#include "wifi_manager.h"
#include <WiFi.h>
#include "wifi_mqtt_secrets.h"

namespace wifimgr {

static const uint32_t RETRY_INTERVAL_MS  = 5000;
static const uint32_t CONNECT_TIMEOUT_MS = 15000;

static uint32_t lastAttemptMs = 0;
static bool     attempting    = false;

static void onWifiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println(F("[WiFi] Mit AP assoziiert, hole IP ..."));
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print(F("[WiFi] IP-Adresse: "));
      Serial.println(WiFi.localIP());
      Serial.print(F("[WiFi] RSSI: "));
      Serial.println(WiFi.RSSI());
      attempting = false;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println(F("[WiFi] Verbindung verloren."));
      attempting = false;
      break;
    default:
      break;
  }
}

static void startAttempt() {
  Serial.print(F("[WiFi] Verbinde mit SSID: "));
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  lastAttemptMs = millis();
  attempting    = true;
}

void WifiBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(onWifiEvent);
  startAttempt();
}

void WifiLoop() {
  if (WiFi.status() == WL_CONNECTED) return;

  uint32_t now = millis();

  if (attempting && (now - lastAttemptMs > CONNECT_TIMEOUT_MS)) {
    Serial.println(F("[WiFi] Timeout, neuer Versuch ..."));
    attempting = false;
  }

  if (!attempting && (now - lastAttemptMs > RETRY_INTERVAL_MS)) {
    startAttempt();
  }
}

bool WifiIsConnected() { return WiFi.status() == WL_CONNECTED; }

String WifiIp() {
  return WifiIsConnected() ? WiFi.localIP().toString() : String("0.0.0.0");
}

}
