#include "game_maze.h"
#include "config.h"
#include "mqtt_manager.h"
#include "physics.h"
#include "game_state.h"
#include <math.h>

static const uint16_t COL_BG     = 0x0000;
static const uint16_t COL_WALL   = 0xFFFF;
static const uint16_t COL_BALL   = 0xF800;
static const uint16_t COL_COOKIE = 0xFFE0;
static const uint16_t COL_TEXT   = 0xFFFF;

static const int   R              = (int)Config::BALL_RAD;
static const int   COOKIE_R       = 3;
static const int   CORRIDOR_EXTRA = 4; // Luft im Korridor über den Kugeldurchmesser hinaus
static const float MAX_SUBSTEP    = (float)R; // Anti-Tunneling: max. Bewegung pro Teilschritt

static const int MAX_COLS    = 18;
static const int MAX_ROWS    = 22;
static const int MAX_COOKIES = 64;

// Wand-Bits pro Zelle
static const uint8_t WALL_N = 0x01;
static const uint8_t WALL_E = 0x02;
static const uint8_t WALL_S = 0x04;
static const uint8_t WALL_W = 0x08;
static const uint8_t VISITED = 0x10;

static Arduino_GFX* s_gfx = nullptr;

static uint8_t  s_maze[MAX_ROWS][MAX_COLS];
static uint16_t s_stack[MAX_ROWS * MAX_COLS]; // expliziter DFS-Stack

static int s_cols, s_rows;
static int s_cell, s_wall;
static int s_originX, s_originY;

static Physics::Ball    s_ball;
static GameState::State s_state;

static float s_prevX, s_prevY;

struct Cookie { int cx, cy; bool active; };
static Cookie s_cookies[MAX_COOKIES];
static int    s_cookieCount;

static bool s_endShown;
static bool s_firstRender;
static bool s_forceRedraw;

static inline int cellPX(int cx) { return s_originX + cx * s_cell; }
static inline int cellPY(int cy) { return s_originY + cy * s_cell; }
static inline int cookieCenterX(int cx) { return cellPX(cx) + s_cell / 2; }
static inline int cookieCenterY(int cy) { return cellPY(cy) + s_cell / 2; }

static void publishEvent(const char* json) {
  mqtt::mqttPublish(Config::PUB_EVENT, json);
}

// Nächster Punkt auf dem Rechteck -> Kreis-Rechteck-Kollision
static bool circleRect(float cxp, float cyp, int rx, int ry, int rw, int rh) {
  float nx = cxp < rx ? rx : (cxp > rx + rw ? rx + rw : cxp);
  float ny = cyp < ry ? ry : (cyp > ry + rh ? ry + rh : cyp);
  float dx = cxp - nx, dy = cyp - ny;
  return (dx * dx + dy * dy) < (float)R * R;
}

static void computeGeometry(uint8_t wallThickness) {
  s_wall = constrain((int)wallThickness, 2, 12);
  s_cell = 2 * s_wall + 2 * R + CORRIDOR_EXTRA;

  int W = s_gfx->width();
  int H = s_gfx->height();
  s_cols = min(W / s_cell, MAX_COLS);
  s_rows = min(H / s_cell, MAX_ROWS);

  int usedW = s_cols * s_cell;
  int usedH = s_rows * s_cell;
  s_originX = (W - usedW) / 2;
  s_originY = (H - usedH) / 2;
}

// Iterativer DFS mit Backtracking – erzeugt ein perfektes Labyrinth
static void generateMaze() {
  for (int cy = 0; cy < s_rows; cy++)
    for (int cx = 0; cx < s_cols; cx++)
      s_maze[cy][cx] = WALL_N | WALL_E | WALL_S | WALL_W;

  int top = 0;
  s_maze[0][0] |= VISITED;
  s_stack[top++] = 0;

  while (top > 0) {
    uint16_t c = s_stack[top - 1];
    int cx = c % s_cols;
    int cy = c / s_cols;

    int nx[4], ny[4], dir[4], n = 0;
    if (cy > 0          && !(s_maze[cy - 1][cx] & VISITED)) { nx[n]=cx;   ny[n]=cy-1; dir[n]=0; n++; }
    if (cx < s_cols - 1 && !(s_maze[cy][cx + 1] & VISITED)) { nx[n]=cx+1; ny[n]=cy;   dir[n]=1; n++; }
    if (cy < s_rows - 1 && !(s_maze[cy + 1][cx] & VISITED)) { nx[n]=cx;   ny[n]=cy+1; dir[n]=2; n++; }
    if (cx > 0          && !(s_maze[cy][cx - 1] & VISITED)) { nx[n]=cx-1; ny[n]=cy;   dir[n]=3; n++; }

    if (n == 0) { top--; continue; } // Sackgasse, zurück

    int k = random(n);
    int wx = nx[k], wy = ny[k];

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

static void placeCookies(uint16_t count) {
  int maxC = min(MAX_COOKIES, s_cols * s_rows - 1); // Startzelle freilassen
  s_cookieCount = constrain((int)count, 1, maxC);

  int placed = 0;
  while (placed < s_cookieCount) {
    int cx = random(s_cols);
    int cy = random(s_rows);
    if (cx == 0 && cy == 0) continue;

    bool dup = false;
    for (int j = 0; j < placed; j++)
      if (s_cookies[j].cx == cx && s_cookies[j].cy == cy) { dup = true; break; }
    if (dup) continue;

    s_cookies[placed++] = { cx, cy, true };
  }
  s_state.cookiesTotal = s_cookieCount;
  s_state.cookiesLeft  = s_cookieCount;
}

static void drawCellWalls(int cx, int cy, uint16_t color) {
  uint8_t w = s_maze[cy][cx];
  int px = cellPX(cx), py = cellPY(cy);
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

static void redrawWallsInRect(int x, int y, int w, int h) {
  int cxMin = max(0, (x - s_originX) / s_cell);
  int cxMax = min(s_cols - 1, (x + w - s_originX) / s_cell);
  int cyMin = max(0, (y - s_originY) / s_cell);
  int cyMax = min(s_rows - 1, (y + h - s_originY) / s_cell);
  for (int cy = cyMin; cy <= cyMax; cy++)
    for (int cx = cxMin; cx <= cxMax; cx++)
      drawCellWalls(cx, cy, COL_WALL);
}

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
  uint32_t sec = GameState::elapsedMs(s_state) / 1000;
  int W = s_gfx->width();

  s_gfx->setTextSize(3);
  s_gfx->setCursor((W - 10 * 18) / 2, 70);
  s_gfx->print("Geschafft!");

  s_gfx->setTextSize(2);
  s_gfx->setCursor(20, 130);
  s_gfx->print("Zeit: "); s_gfx->print(sec); s_gfx->print(" s");
  s_gfx->setCursor(20, 160);
  s_gfx->print("Punkte: "); s_gfx->print(s_state.score);
}

static bool collides(float fx, float fy) {
  int cxMin = max(0, (int)floorf((fx - R - s_originX) / s_cell));
  int cxMax = min(s_cols - 1, (int)floorf((fx + R - s_originX) / s_cell));
  int cyMin = max(0, (int)floorf((fy - R - s_originY) / s_cell));
  int cyMax = min(s_rows - 1, (int)floorf((fy + R - s_originY) / s_cell));

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

void gameMazeInit(Arduino_GFX* gfx, uint8_t wallThickness,
                  uint16_t cookieCount, const char* playerName) {
  s_gfx = gfx;
  randomSeed(esp_random());

  computeGeometry(wallThickness);
  generateMaze();

  float startX = cellPX(0) + s_cell / 2.0f;
  float startY = cellPY(0) + s_cell / 2.0f;
  Physics::resetBall(s_ball, startX, startY);

  GameState::reset(s_state, playerName, cookieCount);
  placeCookies(cookieCount);

  s_prevX = s_ball.x; s_prevY = s_ball.y;
  s_endShown    = false;
  s_firstRender = true;
  s_forceRedraw = false;

  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"event\":\"start\",\"game\":\"maze\",\"player\":\"%s\"}", s_state.player);
  publishEvent(buf);
}

void gameMazeUpdate(float accX, float accY, uint32_t dtMs) {
  if (!s_gfx) return;
  if (s_state.finished || dtMs == 0) return;
  float dt = dtMs / 1000.0f;

  float dx, dy;
  Physics::step(s_ball, accX, accY, dt, dx, dy);

  // Sub-Stepping damit die Kugel nicht durch dünne Wände tunnelt
  int steps = (int)ceilf(max(fabsf(dx), fabsf(dy)) / MAX_SUBSTEP);
  if (steps < 1) steps = 1;
  float sx = dx / steps;
  float sy = dy / steps;

  for (int i = 0; i < steps; i++) {
    float nx = s_ball.x + sx;
    if (collides(nx, s_ball.y)) { s_ball.vx = 0; sx = 0; } else { s_ball.x = nx; }
    float ny = s_ball.y + sy;
    if (collides(s_ball.x, ny)) { s_ball.vy = 0; sy = 0; } else { s_ball.y = ny; }
  }

  // Sicherheits-Clamp falls die Kugel irgendwie außerhalb landet
  float minX = s_originX + s_wall + R;
  float maxX = s_originX + s_cols * s_cell - s_wall - R;
  float minY = s_originY + s_wall + R;
  float maxY = s_originY + s_rows * s_cell - s_wall - R;
  s_ball.x = constrain(s_ball.x, minX, maxX);
  s_ball.y = constrain(s_ball.y, minY, maxY);

  bool wasFinished = s_state.finished;
  for (int i = 0; i < s_cookieCount; i++) {
    if (!s_cookies[i].active) continue;
    float ccx = cookieCenterX(s_cookies[i].cx);
    float ccy = cookieCenterY(s_cookies[i].cy);
    float ddx = s_ball.x - ccx, ddy = s_ball.y - ccy;
    float rr = (float)(R + COOKIE_R);
    if (ddx * ddx + ddy * ddy < rr * rr) {
      s_cookies[i].active = false;
      GameState::collect(s_state);
      s_forceRedraw = true;
      char buf[64];
      snprintf(buf, sizeof(buf),
               "{\"event\":\"cookie\",\"score\":%d,\"left\":%d}",
               s_state.score, s_state.cookiesLeft);
      publishEvent(buf);
    }
  }

  if (!wasFinished && s_state.finished) {
    char buf[80];
    snprintf(buf, sizeof(buf),
             "{\"event\":\"end\",\"game\":\"maze\",\"score\":%d,\"time_s\":%lu}",
             s_state.score, (unsigned long)(GameState::elapsedMs(s_state) / 1000));
    publishEvent(buf);
  }
}

void gameMazeRender() {
  if (!s_gfx) return;

  if (s_state.finished) {
    if (!s_endShown) { showEndScreen(); s_endShown = true; }
    return;
  }

  if (s_firstRender) {
    drawMazeFull();
    s_gfx->fillCircle((int)s_ball.x, (int)s_ball.y, R, COL_BALL);
    s_prevX = s_ball.x; s_prevY = s_ball.y;
    s_firstRender = false;
    return;
  }

  // kein Flackern wenn Pixelposition gleich geblieben
  if (!s_forceRedraw &&
      (int)s_ball.x == (int)s_prevX && (int)s_ball.y == (int)s_prevY) {
    return;
  }
  s_forceRedraw = false;

  // partielles Update: alten Kreis löschen, Wände + Kekse lokal neu zeichnen
  int half = R + COOKIE_R + 1;
  int ex = (int)s_prevX - half;
  int ey = (int)s_prevY - half;
  int ew = 2 * half;
  int eh = 2 * half;
  if (ex < 0) { ew += ex; ex = 0; }
  if (ey < 0) { eh += ey; ey = 0; }
  if (ex + ew > s_gfx->width())  ew = s_gfx->width()  - ex;
  if (ey + eh > s_gfx->height()) eh = s_gfx->height() - ey;

  s_gfx->fillRect(ex, ey, ew, eh, COL_BG);
  redrawWallsInRect(ex, ey, ew, eh);
  redrawCookiesInRect(ex, ey, ew, eh);

  s_gfx->fillCircle((int)s_ball.x, (int)s_ball.y, R, COL_BALL);
  s_prevX = s_ball.x; s_prevY = s_ball.y;
}

bool     gameMazeFinished()    { return s_state.finished; }
uint16_t gameMazeScore()       { return (uint16_t)s_state.score; }
uint16_t gameMazeCookiesLeft() { return (uint16_t)max(0, s_state.cookiesLeft); }
uint32_t gameMazeElapsedMs()   { return GameState::elapsedMs(s_state); }
void gameMazeSetPlayer(const char* p) { GameState::setPlayer(s_state, p); }

void gameMazeBuildStateJson(char* buf, size_t len) {
  GameState::buildJson(s_state, "maze", buf, len);
}
