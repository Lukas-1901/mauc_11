// ===========================================================================
//  MAUC-Projektarbeit Gruppe 11 -- Hauptsketch (Phase 5: zusammengefuehrt)
//  Display + QMI8658-IMU + WLAN + MQTT + State Machine (Menue/Spiel 1/Spiel 2)
//  pin_config.h aus dem Waveshare-Beispiel muss im esp32/-Ordner liegen!
// ===========================================================================
#include "config.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "game_manager.h"

#include <Arduino_GFX_Library.h>
#include "pin_config.h"
#include <Wire.h>
#include "SensorQMI8658.hpp"
#include <time.h>

// ===== Display =============================================================
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX     *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true,
                                          LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// ===== IMU =================================================================
static SensorQMI8658 qmi;
static IMUdata       s_acc;

static void imuSetup() {
  if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    Serial.println("[IMU] QMI8658 nicht gefunden!");
    return;
  }
  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                          SensorQMI8658::ACC_ODR_1000Hz,
                          SensorQMI8658::LPF_MODE_0);
  qmi.enableAccelerometer();
  Serial.printf("[IMU] QMI8658 ok, ChipID=0x%X\n", qmi.getChipID());
}

void imuReadAccelG(float& ax, float& ay) {
  if (qmi.getDataReady()) qmi.getAccelerometer(s_acc.x, s_acc.y, s_acc.z);
  ax = s_acc.x;   // Achsen-/Vorzeichen-Kalibrierung in physics.h / game_maze.cpp
  ay = s_acc.y;
}

// ===== MQTT-Eingang an die State Machine ===================================
static float    g_ax = 0, g_ay = 0;
static uint32_t g_lastStatus = 0;

static void onMqtt(const char* topic, const String& payload) {
  GameManager::handleControl(topic, payload);
}

// ===== NTP (PFLICHT vor TLS) ===============================================
static void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("[NTP] Zeit synchronisieren");
  while (time(nullptr) < 1700000000) { delay(200); Serial.print("."); }
  Serial.println(" ok");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  gfx->begin();
  pinMode(LCD_BL, OUTPUT);
  pinMode(38, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  digitalWrite(38, HIGH);

  imuSetup();

  wifimgr::WifiBegin();
  syncTime();
  mqtt::mqttSetMessageHandler(onMqtt);
  mqtt::mqttSetup(mqtt_server, mqtt_port, mqtt_username, mqtt_password, root_ca);

  GameManager::setup(gfx);   // -> Menue
}

void loop() {
  wifimgr::WifiLoop();
  mqtt::mqttLoop();

  uint32_t jetzt = millis();

  // --- Physik + Render mit fester Rate (50 Hz) ---
  static uint32_t letztePhysik = 0;
  if (jetzt - letztePhysik >= Config::PHYSIK_MS) {
    uint32_t dt = jetzt - letztePhysik;
    if (dt > 100) dt = Config::PHYSIK_MS;   // grosse Luecke (Start/Stall) abfangen
    letztePhysik = jetzt;

    imuReadAccelG(g_ax, g_ay);
    GameManager::update(g_ax, g_ay, dt);
    GameManager::render();
  }

  // --- State + IMU ans Dashboard ---
  if (mqtt::mqttConnected() && jetzt - g_lastStatus >= Config::MQTT_PUBLISH_MS) {
    g_lastStatus = jetzt;

    char buf[160];
    GameManager::buildStateJson(buf, sizeof(buf));
    mqtt::mqttPublish(Config::PUB_STATUS, buf);

    char imu[64];
    snprintf(imu, sizeof(imu), "{\"ax\":%.2f,\"ay\":%.2f}", g_ax, g_ay);
    mqtt::mqttPublish(Config::PUB_IMU, imu);
  }
}
