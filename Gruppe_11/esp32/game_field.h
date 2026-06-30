#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

void gameFieldInit(Arduino_GFX* gfx, uint16_t cookieCount, const char* playerName);
void gameFieldUpdate(float accX, float accY, uint32_t dtMs);
void gameFieldRender();

bool     gameFieldFinished();
uint16_t gameFieldScore();
uint16_t gameFieldCookiesLeft();
uint32_t gameFieldElapsedMs();
void gameFieldSetPlayer(const char* p);

void gameFieldBuildStateJson(char* buf, size_t len);
