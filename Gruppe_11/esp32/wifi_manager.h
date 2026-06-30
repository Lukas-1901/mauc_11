#pragma once
#include <Arduino.h>

namespace wifimgr {

void   WifiBegin();
void   WifiLoop();
bool   WifiIsConnected();
String WifiIp();

}
