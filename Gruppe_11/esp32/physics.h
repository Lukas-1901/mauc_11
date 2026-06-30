#pragma once
#include <Arduino.h>
#include <math.h>

// ===========================================================================
//  Gemeinsames Physik-Grundgeruest fuer BEIDE Spiele.
//  Ablauf eines Schritts: Tiefpassfilter (EMA) -> v += a*dt -> Reibung -> dx,dy.
//
//  Die KOLLISION ist bewusst NICHT hier: Spiel 1 prallt an Labyrinth-Waenden,
//  Spiel 2 reflektiert am Bildschirmrand. Das jeweilige Spielmodul wendet die
//  hier berechnete Verschiebung (dx,dy) an und loest danach seine Kollision.
// ===========================================================================
namespace Physics {

  // ---- Zentrale Tuning-/Kalibrier-Parameter (gelten fuer BEIDE Spiele) ----
  //  Diese Werte aus deiner Spiel-1-Kalibrierung hierher uebernehmen, damit
  //  sich beide Spiele identisch anfuehlen.
  constexpr float ACC_GAIN = 1200.0f;  // px/s^2 pro g (Neigung -> Beschleunigung)
  constexpr float FRICTION = 0.90f;    // Reibung pro Frame (0..1)
  constexpr float ACC_LPF  = 0.40f;    // EMA: Anteil des ALTEN Werts (hoeher = traeger)

  // Achsen an die Display-Ausrichtung anpassen (empirisch, s. Spiel 1):
  constexpr float SIGN_X  = 1.0f;
  constexpr float SIGN_Y  = -1.0f;
  constexpr bool  SWAP_XY = true;

  struct Ball {
    float x, y;       // Position (px)
    float vx, vy;     // Geschwindigkeit (px/s)
    float axF, ayF;   // gefilterte Beschleunigung (EMA-Zustand)
  };

  inline void resetBall(Ball& b, float x, float y) {
    b.x = x; b.y = y;
    b.vx = b.vy = 0.0f;
    b.axF = b.ayF = 0.0f;
  }

  // Ein Integrationsschritt. Liefert die Frame-Verschiebung dx,dy zurueck.
  // Position/Kollision macht das aufrufende Spielmodul.
  inline void step(Ball& b, float accX, float accY, float dt, float& dx, float& dy) {
    // Tiefpass (EMA) zur Glaettung des IMU-Rauschens
    b.axF = ACC_LPF * b.axF + (1.0f - ACC_LPF) * accX;
    b.ayF = ACC_LPF * b.ayF + (1.0f - ACC_LPF) * accY;

    // Achsen-/Vorzeichen-Anpassung
    float ax = b.axF * SIGN_X;
    float ay = b.ayF * SIGN_Y;
    if (SWAP_XY) { float t = ax; ax = ay; ay = t; }

    // v += a*dt, dann Reibung
    b.vx += ax * ACC_GAIN * dt;
    b.vy += ay * ACC_GAIN * dt;
    b.vx *= FRICTION;
    b.vy *= FRICTION;

    dx = b.vx * dt;
    dy = b.vy * dt;
  }
}
