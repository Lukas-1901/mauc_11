#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ===========================================================================
//  Spielmanager / State Machine (Phase 5)
//  Zustaende: MENU -> RUNNING_MAZE/RUNNING_FIELD -> GAME_OVER -> MENU
//  Kapselt die Spielauswahl, das Menue und die Verteilung an die beiden Spiele.
//  esp32.ino ruft nur noch setup/handleControl/update/render auf.
// ===========================================================================
namespace GameManager {

  enum class GState { MENU, RUNNING_MAZE, RUNNING_FIELD, GAME_OVER };

  void setup(Arduino_GFX* gfx);                              // -> MENU
  void handleControl(const char* topic, const String& msg); // MQTT control/*
  void update(float accX, float accY, uint32_t dtMs);        // Physik des aktiven Spiels
  void render();                                             // zeichnet aktiven Zustand
  void buildStateJson(char* buf, size_t len);                // State fuers Dashboard
  GState state();
}
