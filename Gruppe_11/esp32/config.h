#pragma once
#include <stdint.h>

static const char* const MQTT_CLIENT_PREFIX = "mauc-gruppe11-";
static const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;

namespace Config{
    constexpr int BILD_WIDTH = 240;
    constexpr int BILD_HEIGTH = 280;

    constexpr float BALL_RAD = 5.0f;
    constexpr float WAND_BREITE_STD = 10.0f;
    constexpr float IMU_LOWPASS = 0.5f;

    constexpr uint32_t PHYSIK_MS = 20; //50
    constexpr uint32_t MQTT_PUBLISH_MS = 100; //10

    #define MQTT_PREFIX "mauc/gruppe11/"

    constexpr const char* PUB_STATUS = MQTT_PREFIX "status";
    constexpr const char* PUB_IMU = MQTT_PREFIX "imu";
    constexpr const char* PUB_EVENT = MQTT_PREFIX "event";

    constexpr const char* SUB_SPIEL = MQTT_PREFIX "control/spiel";
    constexpr const char* SUB_START = MQTT_PREFIX "control/start";
    constexpr const char* SUB_SPIELER = MQTT_PREFIX "control/spieler";
    constexpr const char* SUB_KEKSE = MQTT_PREFIX "control/kekse";
    constexpr const char* SUB_WAND = MQTT_PREFIX "control/wand";
}