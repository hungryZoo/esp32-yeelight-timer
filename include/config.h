// 장치 설정 상수
#pragma once
#include <Arduino.h>

// WiFi, OTA 비밀번호 등 로컬 전용 값 (.gitignore 대상)
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h 없음: include/secrets.h.example 을 복사해 WiFi 정보를 입력하세요"
#endif

// ---------------------------------------------------------------------------
// 스케줄 기본값 (매일 반복). 웹에서 변경하면 NVS 저장값이 우선.
// OFF가 ON보다 이르면 자정을 넘는 구간으로 처리 (예: ON 18:00, OFF 01:00)
// ---------------------------------------------------------------------------
#define SCHED_ON_TIME        "18:00"
#define SCHED_OFF_TIME       "23:00"
constexpr bool    SCHED_ENABLED       = true;
constexpr uint8_t SCHED_ON_BRIGHTNESS = 0;     // ON 시 밝기 지정 (0 = 변경 없음)

// ---------------------------------------------------------------------------
// 네트워크
// ---------------------------------------------------------------------------
#define HOSTNAME             "candela-timer"
#define NTP_SERVER_1         "pool.ntp.org"
#define NTP_SERVER_2         "time.google.com"
#define NTP_SERVER_3         "kr.pool.ntp.org"
constexpr long     TZ_OFFSET_SEC      = 9 * 3600;   // Asia/Seoul (UTC+9, DST 없음)
constexpr uint32_t WIFI_BOOT_WAIT_MS  = 15000;      // 부팅 시 WiFi 연결 대기 상한
constexpr uint32_t WIFI_BOOT_RETRY_MS = 7000;       // 부팅 대기 중 재시도 간격
constexpr uint32_t WIFI_RETRY_MS      = 15000;      // 끊김 지속 시 재연결 시도 주기

// ---------------------------------------------------------------------------
// 램프
// ---------------------------------------------------------------------------
// 램프 BLE 주소(public). 기본값 빈 문자열 = LAMP_NAME 또는 FE87 광고 스캔으로 탐색 후 NVS 저장.
// 주소 고정이 필요하면 secrets.h 에 #define LAMP_MAC "xx:xx:xx:xx:xx:xx" 지정
#ifndef LAMP_MAC
#define LAMP_MAC   ""
#endif
// Candela 광고 이름 (ESP32 스캔 실측. iOS 앱 목록 표시 이름 yl_candela 와 다름)
#define LAMP_NAME  "yeelight_ms"

// Candela GATT (광고 서비스와 제어 서비스 모두 16-bit FE87)
#define CANDELA_SVC_UUID       "FE87"
#define CANDELA_CTRL_UUID      "AA7D3F34-2D4F-41E0-807F-52FBF8CF7443"
#define CANDELA_NOTIFY_UUID    "8F65073D-9F57-4AAA-AFEA-397D19D5BBEB"

constexpr size_t   CANDELA_CMD_LEN       = 18;      // 명령 길이 (0x00 패딩)
constexpr uint32_t BLE_SCAN_MS           = 8000;    // 주소 탐색 스캔 시간
constexpr uint32_t BLE_CONNECT_TIMEOUT_MS = 6000;
constexpr uint32_t BLE_BACKOFF_MIN_MS    = 2000;    // 재연결 백오프 시작값
constexpr uint32_t BLE_BACKOFF_MAX_MS    = 60000;   // 재연결 백오프 상한
constexpr uint32_t BLE_STATUS_POLL_MS    = 30000;   // 상태 요청 주기 (keepalive 겸용)
constexpr uint32_t BLE_PAIR_TIMEOUT_MS   = 90000;   // 페어링 대기 상한

// ---------------------------------------------------------------------------
// OLED (SSD1306 72x40, I2C) + 버튼
// ---------------------------------------------------------------------------
constexpr uint8_t  OLED_SDA_PIN        = 5;
constexpr uint8_t  OLED_SCL_PIN        = 6;
constexpr uint8_t  OLED_X_OFFSET       = 28;      // 패널 열 오프셋 (U8g2 72x40 기본 28, 화면 잘림 시 29-30)
constexpr uint8_t  OLED_CONTRAST       = 8;       // 0-255, 번인 방지용 저휘도
constexpr uint32_t OLED_SHIFT_MS       = 180000;  // 번인 방지 글자 이동 주기 (3분)
constexpr uint32_t OLED_REFRESH_MS     = 500;

constexpr uint8_t  BUTTON_PIN          = 9;       // BOOT 버튼 (active low)
constexpr uint32_t BUTTON_DEBOUNCE_MS  = 30;
constexpr uint32_t BUTTON_LONG_MS      = 3000;    // 길게 누름 = 페어링 시작

