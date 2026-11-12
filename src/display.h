// SSD1306 72x40 OLED 표시
// 기본 화면: 현재 시각 / 다음 예약 / BLE 상태 / IP. 번인 방지: 저휘도 + 주기적 1-2px 이동.
#pragma once
#include <Arduino.h>

namespace display {

void begin();
void loop();

// 임시 메시지 (ms 동안 기본 화면 대신 표시)
void showMessage(const char* line1, const char* line2, uint32_t ms);
void setHoldMs(uint32_t ms);     // 버튼 누름 유지 시간 (0 = 안 누름)
void setOtaProgress(int pct);    // -1 = OTA 아님

}  // namespace display
