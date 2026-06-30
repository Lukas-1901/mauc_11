#include "game_manager.h"
#include "game_maze.h"
#include "game_field.h"
#include "config.h"
#include "mqtt_manager.h"

namespace GameManager {

// --- Farben ---
static const uint16_t COL_BG    = 0x0000; // schwarz
static const uint16_t COL_TEXT  = 0xFFFF; // weiss
static const uint16_t COL_SEL   = 0xFFE0; // gelb (ausgewaehlt)
static const uint16_t COL_DIM   = 0x7BEF; // grau (nicht ausgewaehlt)

// --- Zustand ---
static Arduino_GFX* s_gfx = nullptr;
static GState       s_state = GState::MENU;

static int   s_selected = 1;                 // 1 = Labyrinth, 2 = freie Flaeche
static uint8_t  s_wand   = (uint8_t)Config::WAND_BREITE_STD;
static uint16_t s_kekse  = 5;
static char  s_spieler[24] = "Player";

// ---------------------------------------------------------------------------
//  MQTT: Zustandswechsel als Event publishen
// ---------------------------------------------------------------------------
static void publishState(const char* name) {
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"event\":\"state\",\"value\":\"%s\"}", name);
  mqtt::mqttPublish(Config::PUB_EVENT, buf);
}

// ---------------------------------------------------------------------------
//  Menue zeichnen (mit Hervorhebung des gewaehlten Spiels)
// ---------------------------------------------------------------------------
static void drawMenu() {
  s_gfx->fillScreen(COL_BG);
  int W = s_gfx->width();

  s_gfx->setTextColor(COL_TEXT);
  s_gfx->setTextSize(2);
  s_gfx->setCursor((W - 11 * 12) / 2, 40);
  s_gfx->print("MENU");

  s_gfx->setTextColor(s_selected == 1 ? COL_SEL : COL_DIM);
  s_gfx->setCursor((W - 11 * 12) / 2, 120);
  s_gfx->print("1 Labyrinth");

  s_gfx->setTextColor(s_selected == 2 ? COL_SEL : COL_DIM);
  s_gfx->setCursor((W - 15 * 12) / 2, 160);
  s_gfx->print("2 Freie Flaeche");
}

static void enterMenu() {
  s_state = GState::MENU;
  drawMenu();
  publishState("menu");
}

// ---------------------------------------------------------------------------
//  Gewaehltes Spiel starten
// ---------------------------------------------------------------------------
static void startSelected() {
  if (s_selected == 1) {
    gameMazeInit(s_gfx, s_wand, s_kekse, s_spieler);
    s_state = GState::RUNNING_MAZE;
    publishState("running_maze");
  } else {
    gameFieldInit(s_gfx, s_kekse, s_spieler);
    s_state = GState::RUNNING_FIELD;
    publishState("running_field");
  }
}

// ===========================================================================
//  OEFFENTLICHE API
// ===========================================================================
void setup(Arduino_GFX* gfx) {
  s_gfx = gfx;
  enterMenu();
}

void handleControl(const char* topic, const String& msg) {
  if (!strcmp(topic, Config::SUB_WAND)) {
    s_wand = msg.toInt();
  } else if (!strcmp(topic, Config::SUB_KEKSE)) {
    s_kekse = msg.toInt();
  } else if (!strcmp(topic, Config::SUB_SPIELER)) {
    strncpy(s_spieler, msg.c_str(), sizeof(s_spieler) - 1);
    s_spieler[sizeof(s_spieler) - 1] = '\0';
  } else if (!strcmp(topic, Config::SUB_SPIEL)) {
    int sel = msg.toInt();
    if (sel == 1 || sel == 2) s_selected = sel;
    // Im Menue/GAME_OVER: Auswahl sichtbar machen bzw. zurueck ins Menue
    if (s_state == GState::MENU || s_state == GState::GAME_OVER) enterMenu();
  } else if (!strcmp(topic, Config::SUB_START)) {
    // Start aus Menue ODER Neustart aus GAME_OVER
    if (s_state != GState::RUNNING_MAZE && s_state != GState::RUNNING_FIELD)
      startSelected();
  }
}

void update(float accX, float accY, uint32_t dtMs) {
  switch (s_state) {
    case GState::RUNNING_MAZE:
      gameMazeUpdate(accX, accY, dtMs);
      if (gameMazeFinished()) { s_state = GState::GAME_OVER; publishState("game_over"); }
      break;
    case GState::RUNNING_FIELD:
      gameFieldUpdate(accX, accY, dtMs);
      if (gameFieldFinished()) { s_state = GState::GAME_OVER; publishState("game_over"); }
      break;
    default:
      break; // MENU / GAME_OVER: keine Physik
  }
}

void render() {
  switch (s_state) {
    case GState::MENU:
      // wird bei Zustandswechsel gezeichnet, hier nichts (kein Flackern)
      break;

    case GState::RUNNING_MAZE:
      gameMazeRender();
      break;

    case GState::RUNNING_FIELD:
      gameFieldRender();
      break;

    case GState::GAME_OVER:
      // Das aktive Spiel zeichnet seinen Endbildschirm (einmalig).
      if (s_selected == 1) gameMazeRender(); else gameFieldRender();
      break;
  }
}

void buildStateJson(char* buf, size_t len) {
  switch (s_state) {
    case GState::RUNNING_MAZE:
    case GState::GAME_OVER:
      if (s_selected == 1) { gameMazeBuildStateJson(buf, len); return; }
      // fall through fuer field
    case GState::RUNNING_FIELD:
      gameFieldBuildStateJson(buf, len);
      return;
    default:
      snprintf(buf, len, "{\"game\":\"menu\",\"player\":\"%s\",\"points\":0,\"time\":0,\"left\":0}",
               s_spieler);
      return;
  }
}

GState state() { return s_state; }

} // namespace GameManager
