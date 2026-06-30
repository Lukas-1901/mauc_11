#include "game_maze.h"
#include "config.h"
#include "mqtt_manager.h"
#include <math.h>

// ===========================================================================
//  KONSTANTEN  (alle Stellschrauben hier oben -- keine Magic Numbers im Code)
// ===========================================================================

// --- Farben als rohe RGB565-Werte (unabhaengig von Makro-Namen der GFX-Lib;
//     je nach Version heissen die Makros BLACK oder RGB565_BLACK -> Hex ist sicher) ---
static const uint16_t COL_BG     = 0x0000; // schwarz
static const uint16_t COL_WALL   = 0xFFFF; // weiss
static const uint16_t COL_BALL   = 0xF800; // rot
static const uint16_t COL_COOKIE = 0xFFE0; // gelb
static const uint16_t COL_TEXT   = 0xFFFF; // weiss

// --- Physik-Tuning (fuer die Ausarbeitung: das sind die wichtigsten Parameter) ---
static const float ACC_GAIN = 800.0f;            // px/s^2 pro g (Neigung -> Beschleunigung)
static const float FRICTION = 0.92f;             // Reibung pro Frame, 0..1 (sonst laeuft Kugel weg)
static const float ACC_LPF  = Config::IMU_LOWPASS; // EMA-Faktor (0.9 = starke Glaettung)

// --- Achsen an die Display-Ausrichtung anpassen (EMPIRISCH verifizieren!) ---
// Board kippen und pruefen, ob die Kugel "bergab" rollt. Falls eine Achse
// verkehrt herum laeuft: Vorzeichen flippen. Falls links/rechts vertauscht: SWAP.
static const float SIGN_X  = 1.0f;
static const float SIGN_Y  = 1.0f;
static const bool  SWAP_XY = false;

// --- Geometrie ---
static const int   R             = (int)Config::BALL_RAD; // Kugelradius
static const int   COOKIE_R      = 3;                     // Keks-Radius
static const int   CORRIDOR_EXTRA= 4;                     // Luft zus. zum Kugeldurchmesser
static const float MAX_SUBSTEP   = (float)R;              // Anti-Tunneling: max. Schritt/Subschritt

// --- Statische Felder dimensionieren (kein dynamischer Speicher) ---
// Kleinste Wand (2px) -> kleinste Zelle -> groesstes Grid. Mit etwas Reserve:
static const int MAX_COLS    = 18;
static const int MAX_ROWS    = 22;
static const int MAX_COOKIES = 64;

// --- Wand-Bits pro Zelle ---
static const uint8_t WALL_N = 0x01;  // oben
static const uint8_t WALL_E = 0x02;  // rechts
static const uint8_t WALL_S = 0x04;  // unten
static const uint8_t WALL_W = 0x08;  // links
static const uint8_t VISITED= 0x10;  // Hilfsbit fuer DFS

// ===========================================================================
//  ZUSTAND (static = nur in dieser Datei sichtbar)
// ===========================================================================
static Arduino_GFX* s_gfx = nullptr;

static uint8_t  s_maze[MAX_ROWS][MAX_COLS];     // Wand-Bits je Zelle
static uint16_t s_stack[MAX_ROWS * MAX_COLS];   // expliziter DFS-Stack

static int s_cols, s_rows;        // Grid-Groesse
static int s_cell, s_wall;        // Zellenpitch / Wandstaerke in px
static int s_originX, s_originY;  // Labyrinth-Ursprung (zentriert)

static float s_x, s_y;            // Kugelposition (px, float)
static float s_vx, s_vy;          // Geschwindigkeit (px/s)
static float s_prevX, s_prevY;    // letzte gezeichnete Position (zum Loeschen)
static float s_axF, s_ayF;        // gefilterte Beschleunigung (EMA-Zustand)

struct Cookie { int cx, cy; bool active; };
static Cookie s_cookies[MAX_COOKIES];
static int    s_cookieCount;
static int    s_cookiesLeft;
static int    s_score;

static uint32_t s_startMs;
static bool s_finished;
static bool s_endShown;
static bool s_firstRender;
static bool s_forceRedraw;        // erzwingt 1x Neuzeichnen (z.B. nach Keks-Fund)
static char s_player[24];

// ===========================================================================
//  HILFSFUNKTIONEN
// ===========================================================================
static inline int cellPX(int cx) { return s_originX + cx * s_cell; }
static inline int cellPY(int cy) { return s_originY + cy * s_cell; }
static inline int cookieCenterX(int cx) { return cellPX(cx) + s_cell / 2; }
static inline int cookieCenterY(int cy) { return cellPY(cy) + s_cell / 2; }

static void publishEvent(const char* json) {
  mqtt::mqttPublish(Config::PUB_EVENT, json);
}

// Kreis (Mittelpunkt cx/cy, Radius R) gegen achsenparalleles Rechteck.
static bool circleRect(float cxp, float cyp, int rx, int ry, int rw, int rh) {
  float nx = cxp < rx ? rx : (cxp > rx + rw ? rx + rw : cxp); // naechster Punkt
  float ny = cyp < ry ? ry : (cyp > ry + rh ? ry + rh : cyp); // auf dem Rechteck
  float dx = cxp - nx, dy = cyp - ny;
  return (dx * dx + dy * dy) < (float)R * R;
}

// ===========================================================================
//  1) GEOMETRIE: Grid an Wandstaerke anpassen
// ===========================================================================
static void computeGeometry(uint8_t wallThickness) {
  s_wall = constrain((int)wallThickness, 2, 12);
  // Korridor-Innenbreite = CELL - 2*WALL. Muss > Kugeldurchmesser (2*R) sein:
  s_cell = 2 * s_wall + 2 * R + CORRIDOR_EXTRA;

  int W = s_gfx->width();
  int H = s_gfx->height();
  s_cols = min(W / s_cell, MAX_COLS);
  s_rows = min(H / s_cell, MAX_ROWS);

  int usedW = s_cols * s_cell;
  int usedH = s_rows * s_cell;
  s_originX = (W - usedW) / 2;   // zentrieren
  s_originY = (H - usedH) / 2;
}

// ===========================================================================
//  2) LABYRINTH-GENERIERUNG: rekursive Tiefensuche (iterativ, mit Backtracking)
// ===========================================================================
static void generateMaze() {
  // Start: alle Waende stehen, nichts besucht
  for (int cy = 0; cy < s_rows; cy++)
    for (int cx = 0; cx < s_cols; cx++)
      s_maze[cy][cx] = WALL_N | WALL_E | WALL_S | WALL_W;

  int top = 0;
  s_maze[0][0] |= VISITED;
  s_stack[top++] = 0;                 // Startzelle (0,0), kodiert als cy*cols+cx

  while (top > 0) {
    uint16_t c = s_stack[top - 1];    // aktuelle Zelle (oben auf dem Stack)
    int cx = c % s_cols;
    int cy = c / s_cols;

    // Unbesuchte Nachbarn sammeln
    int nx[4], ny[4], dir[4], n = 0;
    if (cy > 0          && !(s_maze[cy - 1][cx] & VISITED)) { nx[n]=cx; ny[n]=cy-1; dir[n]=0; n++; } // N
    if (cx < s_cols - 1 && !(s_maze[cy][cx + 1] & VISITED)) { nx[n]=cx+1; ny[n]=cy; dir[n]=1; n++; } // E
    if (cy < s_rows - 1 && !(s_maze[cy + 1][cx] & VISITED)) { nx[n]=cx; ny[n]=cy+1; dir[n]=2; n++; } // S
    if (cx > 0          && !(s_maze[cy][cx - 1] & VISITED)) { nx[n]=cx-1; ny[n]=cy; dir[n]=3; n++; } // W

    if (n == 0) { top--; continue; } // Sackgasse -> Backtracking (pop)

    int k = random(n);               // zufaelligen Nachbarn waehlen
    int wx = nx[k], wy = ny[k];

    // Wand zwischen aktueller Zelle und Nachbar auf BEIDEN Seiten entfernen
    switch (dir[k]) {
      case 0: s_maze[cy][cx] &= ~WALL_N; s_maze[wy][wx] &= ~WALL_S; break;
      case 1: s_maze[cy][cx] &= ~WALL_E; s_maze[wy][wx] &= ~WALL_W; break;
      case 2: s_maze[cy][cx] &= ~WALL_S; s_maze[wy][wx] &= ~WALL_N; break;
      case 3: s_maze[cy][cx] &= ~WALL_W; s_maze[wy][wx] &= ~WALL_E; break;
    }
    s_maze[wy][wx] |= VISITED;
    s_stack[top++] = (uint16_t)(wy * s_cols + wx);
  }
}

// ===========================================================================
//  3) KEKSE PLATZIEREN
// ===========================================================================
static void placeCookies(uint16_t count) {
  int maxC = min(MAX_COOKIES, s_cols * s_rows - 1); // -1: Startzelle frei lassen
  s_cookieCount = constrain((int)count, 1, maxC);

  int placed = 0;
  while (placed < s_cookieCount) {
    int cx = random(s_cols);
    int cy = random(s_rows);
    if (cx == 0 && cy == 0) continue;             // nicht auf Startzelle

    bool dup = false;                              // keine Doppelbelegung
    for (int j = 0; j < placed; j++)
      if (s_cookies[j].cx == cx && s_cookies[j].cy == cy) { dup = true; break; }
    if (dup) continue;

    s_cookies[placed].cx = cx;
    s_cookies[placed].cy = cy;
    s_cookies[placed].active = true;
    placed++;
  }
  s_cookiesLeft = s_cookieCount;
  s_score = 0;
}

// ===========================================================================
//  ZEICHENROUTINEN
// ===========================================================================
static void drawCellWalls(int cx, int cy, uint16_t color) {
  uint8_t w = s_maze[cy][cx];
  int px = cellPX(cx), py = cellPY(cy);
  // Wand-Rechtecke ueberlappen sich an den Ecken -> Ecken werden automatisch
  // sauber gefuellt (loest das Ecken-Rendering ohne Sonderbehandlung).
  if (w & WALL_N) s_gfx->fillRect(px, py, s_cell, s_wall, color);
  if (w & WALL_S) s_gfx->fillRect(px, py + s_cell - s_wall, s_cell, s_wall, color);
  if (w & WALL_W) s_gfx->fillRect(px, py, s_wall, s_cell, color);
  if (w & WALL_E) s_gfx->fillRect(px + s_cell - s_wall, py, s_wall, s_cell, color);
}

static void drawMazeFull() {
  s_gfx->fillScreen(COL_BG);
  for (int cy = 0; cy < s_rows; cy++)
    for (int cx = 0; cx < s_cols; cx++)
      drawCellWalls(cx, cy, COL_WALL);
  for (int i = 0; i < s_cookieCount; i++)
    if (s_cookies[i].active)
      s_gfx->fillCircle(cookieCenterX(s_cookies[i].cx),
                        cookieCenterY(s_cookies[i].cy), COOKIE_R, COL_COOKIE);
}

// Waende in einem Rechteck nachzeichnen (nach dem Loeschen der Kugel).
static void redrawWallsInRect(int x, int y, int w, int h) {
  int cxMin = max(0, (x - s_originX) / s_cell);
  int cxMax = min(s_cols - 1, (x + w - s_originX) / s_cell);
  int cyMin = max(0, (y - s_originY) / s_cell);
  int cyMax = min(s_rows - 1, (y + h - s_originY) / s_cell);
  for (int cy = cyMin; cy <= cyMax; cy++)
    for (int cx = cxMin; cx <= cxMax; cx++)
      drawCellWalls(cx, cy, COL_WALL);
}

// Aktive Kekse in einem Rechteck nachzeichnen.
static void redrawCookiesInRect(int x, int y, int w, int h) {
  for (int i = 0; i < s_cookieCount; i++) {
    if (!s_cookies[i].active) continue;
    int ccx = cookieCenterX(s_cookies[i].cx);
    int ccy = cookieCenterY(s_cookies[i].cy);
    if (ccx + COOKIE_R < x || ccx - COOKIE_R > x + w) continue;
    if (ccy + COOKIE_R < y || ccy - COOKIE_R > y + h) continue;
    s_gfx->fillCircle(ccx, ccy, COOKIE_R, COL_COOKIE);
  }
}

static void showEndScreen() {
  s_gfx->fillScreen(COL_BG);
  s_gfx->setTextColor(COL_TEXT);
  uint32_t sec = gameMazeElapsedMs() / 1000;
  int W = s_gfx->width();

  s_gfx->setTextSize(3);
  s_gfx->setCursor((W - 10 * 18) / 2, 70);  // grob zentriert (10 Zeichen, 18px breit)
  s_gfx->print("Geschafft!");

  s_gfx->setTextSize(2);
  s_gfx->setCursor(20, 130);
  s_gfx->print("Zeit: "); s_gfx->print(sec); s_gfx->print(" s");
  s_gfx->setCursor(20, 160);
  s_gfx->print("Punkte: "); s_gfx->print(s_score);
}

// ===========================================================================
//  4) KOLLISION: Kugel gegen die Waende der Nachbarzellen
// ===========================================================================
static bool collides(float fx, float fy) {
  int cxMin = (int)floorf((fx - R - s_originX) / s_cell);
  int cxMax = (int)floorf((fx + R - s_originX) / s_cell);
  int cyMin = (int)floorf((fy - R - s_originY) / s_cell);
  int cyMax = (int)floorf((fy + R - s_originY) / s_cell);
  cxMin = max(0, cxMin); cxMax = min(s_cols - 1, cxMax);
  cyMin = max(0, cyMin); cyMax = min(s_rows - 1, cyMax);

  for (int cy = cyMin; cy <= cyMax; cy++) {
    for (int cx = cxMin; cx <= cxMax; cx++) {
      uint8_t w = s_maze[cy][cx];
      int px = cellPX(cx), py = cellPY(cy);
      if ((w & WALL_N) && circleRect(fx, fy, px, py, s_cell, s_wall)) return true;
      if ((w & WALL_S) && circleRect(fx, fy, px, py + s_cell - s_wall, s_cell, s_wall)) return true;
      if ((w & WALL_W) && circleRect(fx, fy, px, py, s_wall, s_cell)) return true;
      if ((w & WALL_E) && circleRect(fx, fy, px + s_cell - s_wall, py, s_wall, s_cell)) return true;
    }
  }
  return false;
}

// ===========================================================================
//  OEFFENTLICHE API
// ===========================================================================
void gameMazeInit(Arduino_GFX* gfx, uint8_t wallThickness,
                  uint16_t cookieCount, const char* playerName) {
  s_gfx = gfx;
  randomSeed(esp_random());           // echter Hardware-Zufall -> jedes Mal neu

  computeGeometry(wallThickness);
  generateMaze();

  // Kugel in der Mitte der Startzelle (0,0)
  s_x = cellPX(0) + s_cell / 2.0f;
  s_y = cellPY(0) + s_cell / 2.0f;
  s_vx = s_vy = 0.0f;
  s_axF = s_ayF = 0.0f;
  s_prevX = s_x; s_prevY = s_y;

  placeCookies(cookieCount);

  // Spielername sicher kopieren
  strncpy(s_player, playerName ? playerName : "", sizeof(s_player) - 1);
  s_player[sizeof(s_player) - 1] = '\0';

  s_finished    = false;
  s_endShown    = false;
  s_firstRender = true;
  s_forceRedraw = false;
  s_startMs     = millis();

  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"event\":\"start\",\"game\":\"maze\",\"player\":\"%s\"}", s_player);
  publishEvent(buf);
}

void gameMazeUpdate(float accX, float accY, uint32_t dtMs) {
  if (!s_gfx) return;                 // noch kein Spiel gestartet -> nichts tun
  if (s_finished || dtMs == 0) return;
  float dt = dtMs / 1000.0f;

  // --- Tiefpassfilter (EMA): glaettet IMU-Rauschen ---
  s_axF = ACC_LPF * s_axF + (1.0f - ACC_LPF) * accX;
  s_ayF = ACC_LPF * s_ayF + (1.0f - ACC_LPF) * accY;

  // --- Achsen an Display anpassen ---
  float ax = s_axF * SIGN_X;
  float ay = s_ayF * SIGN_Y;
  if (SWAP_XY) { float t = ax; ax = ay; ay = t; }

  // --- Integration: v += a*dt, dann Reibung ---
  s_vx += ax * ACC_GAIN * dt;
  s_vy += ay * ACC_GAIN * dt;
  s_vx *= FRICTION;
  s_vy *= FRICTION;

  // --- Bewegung mit Sub-Stepping (Anti-Tunneling) + achsen-getrennte Kollision ---
  float dx = s_vx * dt;
  float dy = s_vy * dt;
  int steps = (int)ceilf(max(fabsf(dx), fabsf(dy)) / MAX_SUBSTEP);
  if (steps < 1) steps = 1;
  float sx = dx / steps;
  float sy = dy / steps;

  for (int i = 0; i < steps; i++) {
    // X-Achse zuerst
    float nx = s_x + sx;
    if (collides(nx, s_y)) { s_vx = 0; sx = 0; } else { s_x = nx; }
    // dann Y-Achse (getrennt -> sauberes Gleiten an Ecken)
    float ny = s_y + sy;
    if (collides(s_x, ny)) { s_vy = 0; sy = 0; } else { s_y = ny; }
  }

  // Sicherheits-Clamp ins Labyrinth (falls Float-Rundung doch mal entweicht)
  float minX = s_originX + s_wall + R;
  float maxX = s_originX + s_cols * s_cell - s_wall - R;
  float minY = s_originY + s_wall + R;
  float maxY = s_originY + s_rows * s_cell - s_wall - R;
  s_x = constrain(s_x, minX, maxX);
  s_y = constrain(s_y, minY, maxY);

  // --- Kekse einsammeln ---
  for (int i = 0; i < s_cookieCount; i++) {
    if (!s_cookies[i].active) continue;
    float ccx = cookieCenterX(s_cookies[i].cx);
    float ccy = cookieCenterY(s_cookies[i].cy);
    float ddx = s_x - ccx, ddy = s_y - ccy;
    float rr = (float)(R + COOKIE_R);
    if (ddx * ddx + ddy * ddy < rr * rr) {
      s_cookies[i].active = false;
      s_score++;
      s_cookiesLeft--;
      s_forceRedraw = true;     // sicherstellen, dass der Keks weggezeichnet wird
      char buf[64];
      snprintf(buf, sizeof(buf),
               "{\"event\":\"cookie\",\"score\":%d,\"left\":%d}", s_score, s_cookiesLeft);
      publishEvent(buf);
    }
  }

  // --- Spielende ---
  if (s_cookiesLeft <= 0 && !s_finished) {
    s_finished = true;
    char buf[80];
    snprintf(buf, sizeof(buf),
             "{\"event\":\"end\",\"game\":\"maze\",\"score\":%d,\"time_s\":%lu}",
             s_score, (unsigned long)(gameMazeElapsedMs() / 1000));
    publishEvent(buf);
  }
}

void gameMazeRender() {
  if (!s_gfx) return;                 // noch kein Spiel gestartet -> nicht zeichnen

  // Endbildschirm einmalig zeichnen
  if (s_finished) {
    if (!s_endShown) { showEndScreen(); s_endShown = true; }
    return;
  }

  // Erster Frame: alles zeichnen
  if (s_firstRender) {
    drawMazeFull();
    s_gfx->fillCircle((int)s_x, (int)s_y, R, COL_BALL);
    s_prevX = s_x; s_prevY = s_y;
    s_firstRender = false;
    return;
  }

  // FLACKER-SCHUTZ: nur neu zeichnen, wenn sich die ganzzahlige Pixelposition
  // wirklich aendert (oder ein Keks-Fund ein Neuzeichnen erzwingt). Sonst wuerde
  // die Kugel bei jedem Frame geloescht+neu gemalt -> sichtbares Blinken im Stand.
  if (!s_forceRedraw &&
      (int)s_x == (int)s_prevX && (int)s_y == (int)s_prevY) {
    return;
  }
  s_forceRedraw = false;

  // Partielles Update: alte Kugel loeschen, Umgebung wiederherstellen, neu zeichnen.
  // Loeschbox etwas groesser (R+COOKIE_R+1), damit ein gerade gefressener Keks
  // direkt mit verschwindet.
  int half = R + COOKIE_R + 1;
  int ex = (int)s_prevX - half;
  int ey = (int)s_prevY - half;
  int ew = 2 * half;
  int eh = 2 * half;
  // Box auf Bildschirm clampen
  if (ex < 0) { ew += ex; ex = 0; }
  if (ey < 0) { eh += ey; ey = 0; }
  if (ex + ew > s_gfx->width())  ew = s_gfx->width()  - ex;
  if (ey + eh > s_gfx->height()) eh = s_gfx->height() - ey;

  s_gfx->fillRect(ex, ey, ew, eh, COL_BG);   // loeschen
  redrawWallsInRect(ex, ey, ew, eh);         // Waende lokal wiederherstellen
  redrawCookiesInRect(ex, ey, ew, eh);       // noch vorhandene Kekse wiederherstellen

  s_gfx->fillCircle((int)s_x, (int)s_y, R, COL_BALL);  // Kugel neu zeichnen
  s_prevX = s_x; s_prevY = s_y;
}

bool     gameMazeFinished()    { return s_finished; }
uint16_t gameMazeScore()       { return (uint16_t)s_score; }
uint16_t gameMazeCookiesLeft() { return (uint16_t)max(0, s_cookiesLeft); }
uint32_t gameMazeElapsedMs()   { return millis() - s_startMs; }

void gameMazeBuildStateJson(char* buf, size_t len) {
  snprintf(buf, len,
           "{\"game\":\"maze\",\"player\":\"%s\",\"points\":%d,\"time\":%lu,\"left\":%d}",
           s_player, s_score,
           (unsigned long)(gameMazeElapsedMs() / 1000),
           max(0, s_cookiesLeft));
}
