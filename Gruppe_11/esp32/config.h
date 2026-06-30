#pragma once
#include <stdint.h>

static const char* const MQTT_CLIENT_PREFIX = "mauc-gruppe11-";
static const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;

namespace Config {
    constexpr int BILD_WIDTH = 240;
    constexpr int BILD_HEIGTH = 280;

    constexpr float BALL_RAD = 5.0f;
    constexpr float WAND_BREITE_STD = 10.0f;
    constexpr float IMU_LOWPASS = 0.5f;

    // --- Physik (gemeinsam für Spiel 1 & Spiel 2) ---
    // WICHTIG: REIBUNG und ACCEL_SCALE auf die in Spiel 1 erprobten Werte setzen,
    // damit sich die Steuerung in beiden Spielen identisch anfühlt.
    constexpr float REIBUNG           = 0.92f;   // Geschwindigkeitsdämpfung pro Frame
    constexpr float ACCEL_SCALE       = 800.0f;  // IMU-Einheit (~g) -> Pixel/s^2
    constexpr float REFLECT_DAEMPFUNG = 0.80f;   // Energieverlust bei Reflexion (Spiel 2)

    // --- Spielfeld Spiel 2 (freie Fläche) ---
    constexpr int   RAHMEN_DICKE      = 3;       // Dicke des weißen Rahmens in Pixel
    constexpr float KEKS_RAD          = 4.0f;    // Radius eines Kekses
    constexpr int   MAX_KEKSE         = 20;      // Obergrenze (Array-Größe)
    constexpr float MIN_ABSTAND_START = 40.0f;   // Kekse nicht direkt am Startpunkt
    constexpr float MIN_ABSTAND_KEKSE = 14.0f;   // Kekse nicht überlappend

    // --- Farben (RGB565) ---
    constexpr uint16_t COL_HINTERGRUND = 0x0000; // schwarz
    constexpr uint16_t COL_RAHMEN      = 0xFFFF; // weiß
    constexpr uint16_t COL_KUGEL       = 0xF800; // rot
    constexpr uint16_t COL_KEKS        = 0xFFE0; // gelb

    constexpr uint32_t PHYSIK_MS = 20;
    constexpr uint32_t MQTT_PUBLISH_MS = 100;

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