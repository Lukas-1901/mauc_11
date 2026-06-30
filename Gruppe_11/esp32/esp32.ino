#include "config.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "game_maze.h"

#include <Arduino_GFX_Library.h>
#include "pin_config.h"
#include <Wire.h>
#include "SensorQMI8658.hpp"
#include <time.h>

unsigned long lastTestPublish = 0;

HWCDC USBSerial;
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST /* RST */, 0 /* rotation */, true /* IPS */, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

static uint8_t g_wand = (uint8_t)Config::WAND_BREITE_STD;
static uint16_t g_kekse = 5;
static char g_spieler[24] = "Spieler";
static uint32_t g_lastStatus = 0;
static float g_ax = 0.0f, g_ay = 0.0f;
static bool g_running = false; 

static SensorQMI8658 qmi;
static IMUdata s_acc;

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
  if (qmi.getDataReady()) {
    qmi.getAccelerometer(s_acc.x, s_acc.y, s_acc.z);
  }
  ax = s_acc.x;
  ay = s_acc.y;
}

static void onMqtt(const char* topic, const String& payload) {
  if (!strcmp(topic, Config::SUB_WAND)) g_wand  = payload.toInt();
  else if (!strcmp(topic, Config::SUB_KEKSE)) g_kekse = payload.toInt();
  else if (!strcmp(topic, Config::SUB_SPIELER)) {
    strncpy(g_spieler, payload.c_str(), 23);
    g_spieler[23] = 0;
    }
  else if (!strcmp(topic, Config::SUB_START)) { gameMazeInit(gfx, g_wand, g_kekse, g_spieler); g_running = true; }
}

static void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");   // UTC
  Serial.print("[NTP] Zeit synchronisieren");
  time_t now = time(nullptr);
  while (now < 1700000000) {
    delay(200);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.printf("\n[NTP] gesetzt: %ld\n", (long)now);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  gfx->begin();
  pinMode(LCD_BL, OUTPUT);
  pinMode(38, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  digitalWrite(38, HIGH);
  gfx->fillScreen(0x0000);

  imuSetup();

  wifimgr::WifiBegin();
  syncTime();
  mqtt::mqttSetMessageHandler(onMqtt);
  mqtt::mqttSetup(mqtt_server, mqtt_port, mqtt_username, mqtt_password, root_ca);

  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(2);
  gfx->setCursor(10, 130);
  gfx->print("Warte auf Start...");
}

void loop() {
  wifimgr::WifiLoop();
  mqtt::mqttLoop();

  uint32_t jetzt = millis();
  static uint32_t letztePhysik = 0;

  if (g_running && jetzt - letztePhysik >= Config::PHYSIK_MS) {
    imuReadAccelG(g_ax, g_ay);
    gameMazeUpdate(g_ax, g_ay, jetzt - letztePhysik);
    letztePhysik = jetzt;      gameMazeRender();
  }

  if(mqtt::mqttConnected() && jetzt - g_lastStatus >= Config::MQTT_PUBLISH_MS){
    g_lastStatus = jetzt;

    char buf[160];
    gameMazeBuildStateJson(buf, sizeof(buf));
    mqtt::mqttPublish(Config::PUB_STATUS, buf);

    char imu[64];
    snprintf(imu, sizeof(imu), "{\"ax\":%.2f,\"ay\":%.2f}", g_ax, g_ay);
    mqtt::mqttPublish(Config::PUB_IMU, imu);
  }
}