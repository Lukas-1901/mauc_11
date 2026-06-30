#pragma once
#include <Arduino.h>

namespace wifimgr {

void   WifiBegin();        // einmal in setup()
void   WifiLoop();         // jeden Durchlauf in loop() — non-blocking
bool   WifiIsConnected();
String WifiIp();           // "0.0.0.0" wenn nicht verbunden

} // namespace wifimgr