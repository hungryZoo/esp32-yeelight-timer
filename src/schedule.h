// 매일 ON/OFF 스케줄
// - ON, OFF 시각의 분 단위 이벤트로 전송, 같은 분 중복 전송 방지
// - 시각 확보 직후(부팅) 또는 스케줄 활성화 시 ON-OFF 구간이면 즉시 ON
#pragma once
#include <Arduino.h>

namespace schedule {

struct Config {
    bool     enabled;
    uint16_t onMin;         // 자정 기준 분 (0-1439)
    uint16_t offMin;
    uint8_t  onBrightness;  // 0 = 변경 없음
};

void   begin();             // NVS 로드 (없으면 config.h 기본값)
void   loop();
Config get();
bool   set(const Config& c);   // 검증 후 NVS 저장, 활성 상태면 구간 판정 적용

bool   inWindow(uint16_t minuteOfDay);
bool   nextEvent(bool& isOn, uint16_t& minute);   // 활성 + 시각 유효 시 true
String lastAction();         // 최근 스케줄 동작 기록

String   fmtHM(uint16_t minuteOfDay);   // "HH:MM"
bool     parseHM(const String& s, uint16_t& minuteOfDay);
uint16_t nowMinute();                   // 현재 시각 분 (시각 무효 시 0xFFFF)

}  // namespace schedule
