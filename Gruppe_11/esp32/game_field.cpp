#include "game_field.h"
#include "config.h"
#include "mqtt_manager.h"
#include "physics.h"
#include "game_state.h"
#include <math.h>

static const uint16_t COL_BG     = 0x0000;
static const uint16_t COL_BORDER = 0xFFFF;
static const uint16_t COL_BALL   = 0xF800;
static const uint16_t COL_COOKIE = 0xFFE0;
static const uint16_t COL_TEXT   = 0xFFFF;

static const int   R              = (int)Config::BALL_RAD;
static const int   COOKIE_R       = 3;
static const int   BORDER         = 4;
static const float DAMP           = 0.80f;
static const int   MIN_START_DIST = 30;
static const int   MAX_COOKIES    = 64;

static Arduino_GFX*     s_gfx = nullptr;
static Physics::Ball    s_ball;
static GameState::State s_state;

struct Cookie { float x, y; bool active; };
static Cookie s_cookies[MAX_COOKIES];
static int    s_cookieCount;

static int   s_W, s_H;
static float s_minX, s_maxX, s_minY, s_maxY; // Kugelmittelpunkt bleibt innerhalb
static float s_startX, s_startY;

static float s_prevX, s_prevY;
static bool  s_firstRender;
static bool  s_forceRedraw;
static bool  s_endShown;

static void publishEvent(const char* json) {
  mqtt::mqttPublish(Config::PUB_EVENT, json);
}

static bool rectsOverlap(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh) {
  return !(ax + aw <= bx || bx + bw <= ax || ay + ah <= by || by + bh <= ay);
}

static void placeCookies(uint16_t count) {
  s_cookieCount = constrain((int)count, 1, MAX_COOKIES);

  int placed = 0, guard = 0;
  while (placed < s_cookieCount && guard < 5000) {
    guard++;
    float x = s_minX + random((int)(s_maxX - s_minX));
    float y = s_minY + random((int)(s_maxY - s_minY));

    float ddx = x - s_startX, ddy = y - s_startY;
    if (ddx * ddx + ddy * ddy < (float)MIN_START_DIST * MIN_START_DIST) continue;

    bool tooClose = false;
    float minSep = 3.0f * (R + COOKIE_R);
    for (int j = 0; j < placed; j++) {
      float ex = x - s_cookies[j].x, ey = y - s_cookies[j].y;
      if (ex * ex + ey * ey < minSep * minSep) { tooClose = true; break; }
    }
    if (tooClose) continue;

    s_cookies[placed++] = { x, y, true };
  }
  // tatsächlich platzierte Anzahl übernehmen (guard könnte früher abgebrochen haben)
  s_cookieCount        = placed;
  s_state.cookiesTotal = placed;
  s_state.cookiesLeft  = placed;
}

static void drawBorder(int clipX, int clipY, int clipW, int clipH, bool full) {
  struct Bar { int x, y, w, h; } bars[4] = {
    { 0, 0, s_W, BORDER },
    { 0, s_H - BORDER, s_W, BORDER },
    { 0, 0, BORDER, s_H },
    { s_W - BORDER, 0, BORDER, s_H }
  };
  for (int i = 0; i < 4; i++) {
    if (full || rectsOverlap(clipX, clipY, clipW, clipH, bars[i].x, bars[i].y, bars[i].w, bars[i].h))
      s_gfx->fillRect(bars[i].x, bars[i].y, bars[i].w, bars[i].h, COL_BORDER);
  }
}

static void drawFieldFull() {
  s_gfx->fillScreen(COL_BG);
  drawBorder(0, 0, 0, 0, true);
  for (int i = 0; i < s_cookieCount; i++)
    if (s_cookies[i].active)
      s_gfx->fillCircle((int)s_cookies[i].x, (int)s_cookies[i].y, COOKIE_R, COL_COOKIE);
}

static void redrawCookiesInRect(int x, int y, int w, int h) {
  for (int i = 0; i < s_cookieCount; i++) {
    if (!s_cookies[i].active) continue;
    int cx = (int)s_cookies[i].x, cy = (int)s_cookies[i].y;
    if (cx + COOKIE_R < x || cx - COOKIE_R > x + w) continue;
    if (cy + COOKIE_R < y || cy - COOKIE_R > y + h) continue;
    s_gfx->fillCircle(cx, cy, COOKIE_R, COL_COOKIE);
  }
}

static void showEndScreen() {
  s_gfx->fillScreen(COL_BG);
  s_gfx->setTextColor(COL_TEXT);
  uint32_t sec = gameFieldElapsedMs() / 1000;

  s_gfx->setTextSize(3);
  s_gfx->setCursor((s_W - 10 * 18) / 2, 70);
  s_gfx->print("Geschafft!");

  s_gfx->setTextSize(2);
  s_gfx->setCursor(20, 130);
  s_gfx->print("Zeit: "); s_gfx->print(sec); s_gfx->print(" s");
  s_gfx->setCursor(20, 160);
  s_gfx->print("Punkte: "); s_gfx->print(s_state.score);
}

void gameFieldInit(Arduino_GFX* gfx, uint16_t cookieCount, const char* playerName) {
  s_gfx = gfx;
  randomSeed(esp_random());

  s_W = gfx->width();
  s_H = gfx->height();

  s_minX = BORDER + R;  s_maxX = s_W - BORDER - R;
  s_minY = BORDER + R;  s_maxY = s_H - BORDER - R;

  s_startX = s_W / 2.0f;
  s_startY = s_H / 2.0f;
  Physics::resetBall(s_ball, s_startX, s_startY);

  GameState::reset(s_state, playerName, cookieCount);
  placeCookies(cookieCount);

  s_prevX = s_ball.x; s_prevY = s_ball.y;
  s_firstRender = true;
  s_forceRedraw = false;
  s_endShown    = false;

  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"event\":\"start\",\"game\":\"field\",\"player\":\"%s\"}", s_state.player);
  publishEvent(buf);
}

void gameFieldUpdate(float accX, float accY, uint32_t dtMs) {
  if (!s_gfx) return;
  if (s_state.finished || dtMs == 0) return;
  float dt = dtMs / 1000.0f;

  float dx, dy;
  Physics::step(s_ball, accX, accY, dt, dx, dy);
  s_ball.x += dx;
  s_ball.y += dy;

  // Randkollision: elastisch zurückprallen
  if (s_ball.x < s_minX)      { s_ball.x = s_minX; s_ball.vx = -s_ball.vx * DAMP; }
  else if (s_ball.x > s_maxX) { s_ball.x = s_maxX; s_ball.vx = -s_ball.vx * DAMP; }
  if (s_ball.y < s_minY)      { s_ball.y = s_minY; s_ball.vy = -s_ball.vy * DAMP; }
  else if (s_ball.y > s_maxY) { s_ball.y = s_maxY; s_ball.vy = -s_ball.vy * DAMP; }

  bool wasFinished = s_state.finished;
  for (int i = 0; i < s_cookieCount; i++) {
    if (!s_cookies[i].active) continue;
    float ex = s_ball.x - s_cookies[i].x;
    float ey = s_ball.y - s_cookies[i].y;
    float rr = (float)(R + COOKIE_R);
    if (ex * ex + ey * ey < rr * rr) {
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
             "{\"event\":\"end\",\"game\":\"field\",\"score\":%d,\"time_s\":%lu}",
             s_state.score, (unsigned long)(GameState::elapsedMs(s_state) / 1000));
    publishEvent(buf);
  }
}

void gameFieldRender() {
  if (!s_gfx) return;

  if (s_state.finished) {
    if (!s_endShown) { showEndScreen(); s_endShown = true; }
    return;
  }

  if (s_firstRender) {
    drawFieldFull();
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

  // alten Kreis löschen, Rand + Kekse lokal wiederherstellen, neu zeichnen
  int half = R + COOKIE_R + 1;
  int ex = (int)s_prevX - half;
  int ey = (int)s_prevY - half;
  int ew = 2 * half;
  int eh = 2 * half;
  if (ex < 0) { ew += ex; ex = 0; }
  if (ey < 0) { eh += ey; ey = 0; }
  if (ex + ew > s_W) ew = s_W - ex;
  if (ey + eh > s_H) eh = s_H - ey;

  s_gfx->fillRect(ex, ey, ew, eh, COL_BG);
  drawBorder(ex, ey, ew, eh, false);
  redrawCookiesInRect(ex, ey, ew, eh);

  s_gfx->fillCircle((int)s_ball.x, (int)s_ball.y, R, COL_BALL);
  s_prevX = s_ball.x; s_prevY = s_ball.y;
}

bool     gameFieldFinished()    { return s_state.finished; }
uint16_t gameFieldScore()       { return (uint16_t)s_state.score; }
uint16_t gameFieldCookiesLeft() { return (uint16_t)max(0, s_state.cookiesLeft); }
uint32_t gameFieldElapsedMs()   { return GameState::elapsedMs(s_state); }
void gameFieldSetPlayer(const char* p) { GameState::setPlayer(s_state, p); }

void gameFieldBuildStateJson(char* buf, size_t len) {
  GameState::buildJson(s_state, "field", buf, len);
}
