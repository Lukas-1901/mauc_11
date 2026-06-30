#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ===========================================================================
//  Spiel 2: Freie Flaeche -- Kugel mit Reflexion am Rand, Kekse sammeln.
//  Gleiche Schnittstelle wie Spiel 1 (nur ohne Wandstaerke).
//  Nutzt die gemeinsamen Module physics.h und game_state.h.
// ===========================================================================

void gameFieldInit(Arduino_GFX* gfx, uint16_t cookieCount, const char* playerName);
void gameFieldUpdate(float accX, float accY, uint32_t dtMs);
void gameFieldRender();

bool     gameFieldFinished();
uint16_t gameFieldScore();
uint16_t gameFieldCookiesLeft();
uint32_t gameFieldElapsedMs();

void gameFieldBuildStateJson(char* buf, size_t len);
