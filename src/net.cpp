#include "net.h"

#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include "config.h"
#include "log.h"

namespace {
bool          g_wasConnected = false;
uint32_t      g_lostAt       = 0;
uint32_t      g_lastRetry    = 0;
volatile bool g_syncEvent    = false;   // SNTP 콜백 (lwIP 태스크)
time_t        g_lastSync     = 0;

void onSntpSync(struct timeval*) { g_syncEvent = true; }

void handleSync() {
    if (!g_syncEvent) return;
    g_syncEvent    = false;
    bool first     = g_lastSync == 0;
    g_lastSync     = time(nullptr);
    struct tm t;
    localtime_r(&g_lastSync, &t);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &t);
    logf("NTP", "%s %s KST", first ? "동기화 완료" : "재동기화", buf);
}
}  // namespace

namespace net {

void initClock() {
    // 소프트 리셋 후 RTC 시각이 유지되므로 NTP 전에도 KST로 표기되도록 TZ 우선 설정
    setenv("TZ", "KST-9", 1);
    tzset();
}

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    // BLE와 공존하려면 modem sleep 유지 필요 (WiFi.setSleep(false) 금지)
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    logf("WIFI", "연결 시도 SSID=%s", WIFI_SSID);

    // 리셋 직후 드라이버가 IDLE(status 0)에 머무는 경우가 있어 대기 중에도 주기적으로 재시도
    uint32_t t0 = millis(), tTry = t0;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_BOOT_WAIT_MS) {
        if (millis() - tTry >= WIFI_BOOT_RETRY_MS) {
            tTry = millis();
            logf("WIFI", "재시도 (status %d)", (int)WiFi.status());
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        g_wasConnected = true;
        logf("WIFI", "연결됨 IP=%s RSSI=%d (%lums)", WiFi.localIP().toString().c_str(), WiFi.RSSI(),
             (unsigned long)(millis() - t0));
    } else {
        g_lostAt    = millis();
        g_lastRetry = millis();
        logf("WIFI", "부팅 대기 시간 내 연결 실패 (status %d), 백그라운드 재시도", (int)WiFi.status());
    }

    // SNTP는 WiFi 연결 후 자동 동기화, 이후 1시간 주기 재동기화
    sntp_set_time_sync_notification_cb(onSntpSync);
    configTime(TZ_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

    uint32_t t1 = millis();
    while (g_wasConnected && !g_syncEvent && millis() - t1 < 5000) {
        delay(100);
    }
    handleSync();
    if (g_lastSync == 0) {
        logf("NTP", timeValid() ? "동기화 대기, 리셋 전 RTC 시각 사용" : "동기화 대기, 시각 미확보");
    }
}

void loop() {
    uint32_t now = millis();
    bool     up  = WiFi.status() == WL_CONNECTED;

    if (up && !g_wasConnected) {
        logf("WIFI", "연결됨 IP=%s RSSI=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else if (!up && g_wasConnected) {
        logf("WIFI", "연결 끊김");
        g_lostAt    = now;
        g_lastRetry = now;
    }
    g_wasConnected = up;

    // 자동 재연결이 장시간 실패하면 명시적 재시도
    if (!up && now - g_lastRetry >= WIFI_RETRY_MS) {
        g_lastRetry = now;
        logf("WIFI", "재연결 시도 (끊김 %lus)", (unsigned long)((now - g_lostAt) / 1000));
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    handleSync();
}

bool   wifiConnected() { return WiFi.status() == WL_CONNECTED; }
bool   timeValid() { return time(nullptr) > 1700000000; }
time_t lastSync() { return g_lastSync; }
String ip() { return WiFi.localIP().toString(); }
int    rssi() { return WiFi.RSSI(); }

}  // namespace net
