// WiFi 연결 유지 + NTP 시각 동기화
#pragma once
#include <Arduino.h>
#include <time.h>

namespace net {

void   initClock();      // TZ 설정 (setup 최초 호출)
void   begin();          // WiFi 시작, 최대 WIFI_BOOT_WAIT_MS 대기, configTime
void   loop();           // 재연결, NTP 동기화 감지
bool   wifiConnected();
bool   timeValid();      // 시각 유효 여부 (NTP 또는 소프트 리셋 전 RTC 유지 시각)
time_t lastSync();       // 마지막 NTP 동기화 시각 (0 = 없음)
String ip();
int    rssi();

}  // namespace net
