#include "config.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include <time.h>
#include <Arduino_GFX_Library.h>

unsigned long lastTestPublish = 0;

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
extern Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST /* RST */, 0 /* rotation */, true /* IPS */, LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

static uint8_t g_wand = (uint8_t)Config::WAND_BREITE_STD;
static uint16_t g_kekse = 5;
static char g_spieler[24] = "Spieler";
static uint32_t g_lastStatus = 0;

static void onMqtt(const char* topic, const String& payload){
  if (!strcmp(topic, Config::SUB_WAND)) g_wand = payload.toInt();
  else if (!strcmp(topic, Config::SUB_KEKSE)) g_kekse = payload.toInt();
  else if (!strcmp(topic, Config::SUB_SPIELER)) {
    strncpy(g_spieler, payload.c_str(), 23);
    g_pspieler[23] = 0;
  }
  else if(!strcmp(topic, Config::SUB_START)) gameMazeInit(gfx, g_wand, g_kekse, g_spieler);
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

  gfx->begin()
  gfx->fillScreen(RGB565_BLACK);

  wifimgr::WifiBegin();
  syncTime();
  mqtt:mqttSetup(mqtt_server, mqtt_port, mqtt_username, mqtt_password, root_ca);
  mqtt_mqttSetMessageHandler(onMqtt);
}

void loop() {
  wifimgr::WifiLoop();
  mqtt:mqttLoop();
  
  uint32_t jetzt = millis();
  static uint32_t letztePhysik = 0;
  if (jetzt - letztePhysik >= Config::PHYSIK_MS){
    float ax, ay;
    imuReadAccelG(ax, ay);
    gameMazeUpdate(ax, ay, jetzt - lastPhysik);
    lastPhysik = jetzt;
    gameMazeRender();
  }

  if(mqtt:mqttConnected() && jetzt - g_lastStatus >= Config::MQTT_PUBLISH_MS){
    g_lastStatus = jetzt;
    char buf[160];
    gameMazeBuildStateJson(buf, sizeof(buf));
    mqtt:mqttPublish(Config::PUB_STATE, buf);
  }
}