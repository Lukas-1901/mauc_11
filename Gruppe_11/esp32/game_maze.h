#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ===========================================================================
//  Spiel 1: Labyrinth-Kekse-Sammler   (MAUC-Projektarbeit, Gruppe 11)
// ---------------------------------------------------------------------------
//  Bewusst schlanke Schnittstelle:
//   * Das Display-Objekt (Arduino_GFX*) wird von aussen (Hauptsketch) erzeugt
//     und hereingereicht  -> game_maze kennt die Board-Pins / PSRAM-Details
//     NICHT und kann nichts an der funktionierenden Display-Init kaputtmachen.
//   * Die Beschleunigung wird als Parameter uebergeben (das .ino liest die IMU
//     und ruft update auf) -> Modul ist unabhaengig von imu_manager-Details
//     und laesst sich in Phase 5 leicht mergen.
//   * Schnittstelle folgt dem Phase-5-Muster: init() / update(dt) / render().
// ===========================================================================

// Labyrinth neu erzeugen und Spiel starten.
//   gfx           : bereits initialisiertes Display-Objekt
//   wallThickness : Wandstaerke in px (vom Dashboard, control/wall)
//   cookieCount   : Anzahl Kekse (vom Dashboard, control/cookies)
//   playerName    : Spielername (vom Dashboard, control/player)
void gameMazeInit(Arduino_GFX* gfx,
                  uint8_t  wallThickness,
                  uint16_t cookieCount,
                  const char* playerName);

// Physikschritt: Tiefpassfilter -> v += a*dt -> Reibung -> pos += v*dt mit
// Wandkollision (Sub-Stepping) -> Kekse einsammeln. Zeichnet NICHT.
//   accX, accY : Beschleunigung in g (roh oder leicht vorgefiltert)
//   dtMs       : vergangene Zeit seit letztem Update in Millisekunden
void gameMazeUpdate(float accX, float accY, uint32_t dtMs);

// Zeichnen. Erster Aufruf nach init(): komplettes Labyrinth + Kekse.
// Danach nur partielles Update (alte Kugel loeschen, Waende/Kekse lokal
// nachzeichnen, Kugel neu zeichnen). Bei Spielende: Endbildschirm.
void gameMazeRender();

bool     gameMazeFinished();      // true, sobald alle Kekse gesammelt
uint16_t gameMazeScore();         // aktuelle Punktzahl
uint16_t gameMazeCookiesLeft();   // verbleibende Kekse
uint32_t gameMazeElapsedMs();     // Zeit seit Spielstart in ms

// Kompaktes State-JSON fuer periodisches Publish (im .ino, alle MQTT_PUBLISH_MS).
//   z.B. {"game":"maze","player":"Tim","points":3,"time":12,"left":2}
void gameMazeBuildStateJson(char* buf, size_t len);
