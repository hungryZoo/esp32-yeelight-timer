// Yeelight Candela BLE 제어
// 별도 FreeRTOS 태스크에서 연결, 재연결(백오프), 명령 전송 처리.
// 외부 API는 요청 플래그만 설정하므로 어느 태스크에서 호출해도 비차단.
#pragma once
#include <Arduino.h>

namespace candela {

enum class Link : uint8_t { Disconnected, Scanning, Connecting, Connected };

struct Status {
    Link     link;
    bool     authed;      // 이번 연결에서 페어링 인증 완료 (43 63 02 수신)
    bool     paired;      // 이 장치로 페어링한 이력 (NVS)
    bool     pairing;     // 페어링 진행 중 (램프 링 회전 대기)
    int8_t   power;       // -1 미확인, 0 꺼짐, 1 켜짐
    int16_t  brightness;  // -1 미확인, 1-100
    int8_t   rssi;
    bool     haveAddr;
    char     addr[18];
    uint32_t connects;    // 연결 성공 횟수
    uint32_t drops;       // 연결 끊김 횟수
    uint32_t connectedMs; // 현재 연결 유지 시간
    uint32_t nextRetryMs; // 미연결 시 다음 재시도까지 남은 시간
    int8_t   pendingPower;      // 전송 대기 중인 전원 요청 (-1 없음)
    int16_t  pendingBrightness; // 전송 대기 중인 밝기 요청 (-1 없음)
};

void begin();

void setPower(bool on);
void togglePower();
void setBrightness(uint8_t pct);
void requestStatus();
void startPairing();
void forgetPairing();   // NVS 페어링 이력 삭제
void rescan();          // 저장 주소 삭제 후 이름 스캔으로 재탐색

Status      status();
bool        isConnected();
const char* linkName(Link l);
const char* badge();    // OLED 표시용: "OK", "X", "PAIR"

}  // namespace candela
