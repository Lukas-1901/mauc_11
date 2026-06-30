#pragma once
#include <Arduino.h>

// Gemeinsame Punkte-/Zeit-/Spieler-Logik für beide Spiele
namespace GameState {

  struct State {
    char     player[24];
    int      score;
    int      cookiesTotal;
    int      cookiesLeft;
    uint32_t startMs;
    bool     finished;
  };

  inline void reset(State& s, const char* player, int cookies) {
    strncpy(s.player, player ? player : "", sizeof(s.player) - 1);
    s.player[sizeof(s.player) - 1] = '\0';
    s.score        = 0;
    s.cookiesTotal = cookies;
    s.cookiesLeft  = cookies;
    s.startMs      = millis();
    s.finished     = false;
  }

  inline uint32_t elapsedMs(const State& s) { return millis() - s.startMs; }

  inline void collect(State& s) {
    s.score++;
    s.cookiesLeft--;
    if (s.cookiesLeft <= 0) s.finished = true;
  }

  inline void setPlayer(State& s, const char* player) {
    strncpy(s.player, player ? player : "", sizeof(s.player) - 1);
    s.player[sizeof(s.player) - 1] = '\0';
  }

  inline void buildJson(const State& s, const char* game, char* buf, size_t n) {
    int left = s.cookiesLeft < 0 ? 0 : s.cookiesLeft;
    snprintf(buf, n,
      "{\"game\":\"%s\",\"player\":\"%s\",\"points\":%d,\"time\":%lu,\"left\":%d}",
      game, s.player, s.score, (unsigned long)(elapsedMs(s) / 1000), left);
  }
}
