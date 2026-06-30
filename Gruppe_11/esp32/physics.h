#pragma once
#include <Arduino.h>
#include <math.h>

// Gemeinsames Physik-Modul: Tiefpass -> v += a*dt -> Reibung -> liefert dx/dy.
// Kollision macht das jeweilige Spielmodul selbst.
namespace Physics {

  constexpr float ACC_GAIN = 1200.0f;  // px/s² pro g
  constexpr float FRICTION = 0.90f;
  constexpr float ACC_LPF  = 0.40f;   // EMA-Koeffizient, höher = träger

  // Achsen empirisch anpassen bis die Neigung stimmt
  constexpr float SIGN_X  = 1.0f;
  constexpr float SIGN_Y  = -1.0f;
  constexpr bool  SWAP_XY = true;

  struct Ball {
    float x, y;
    float vx, vy;
    float axF, ayF; // EMA-Zustand
  };

  inline void resetBall(Ball& b, float x, float y) {
    b.x = x; b.y = y;
    b.vx = b.vy = 0.0f;
    b.axF = b.ayF = 0.0f;
  }

  // Einen Schritt berechnen; Position/Kollision macht der Aufrufer.
  inline void step(Ball& b, float accX, float accY, float dt, float& dx, float& dy) {
    b.axF = ACC_LPF * b.axF + (1.0f - ACC_LPF) * accX;
    b.ayF = ACC_LPF * b.ayF + (1.0f - ACC_LPF) * accY;

    float ax = b.axF * SIGN_X;
    float ay = b.ayF * SIGN_Y;
    if (SWAP_XY) { float t = ax; ax = ay; ay = t; }

    b.vx += ax * ACC_GAIN * dt;
    b.vy += ay * ACC_GAIN * dt;
    b.vx *= FRICTION;
    b.vy *= FRICTION;

    dx = b.vx * dt;
    dy = b.vy * dt;
  }
}
