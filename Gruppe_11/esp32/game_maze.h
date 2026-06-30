#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

void gameMazeInit(Arduino_GFX* gfx,
                  uint8_t  wallThickness,
                  uint16_t cookieCount,
                  const char* playerName);

void gameMazeUpdate(float accX, float accY, uint32_t dtMs);

void gameMazeRender();

bool     gameMazeFinished();
uint16_t gameMazeScore();
uint16_t gameMazeCookiesLeft();
uint32_t gameMazeElapsedMs();

void gameMazeBuildStateJson(char* buf, size_t len);
