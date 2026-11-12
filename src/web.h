// 웹 제어 서버 (포트 80, http://candela-timer.local/)
#pragma once
#include <Arduino.h>

namespace web {

void begin();   // WiFi 연결 후 1회 호출 (MDNS는 ArduinoOTA가 시작)
void loop();

}  // namespace web
