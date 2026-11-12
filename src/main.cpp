// Yeelight Candela BLE 타이머
// BLE + WiFi/NTP + 스케줄 + OLED + 버튼 + OTA + 웹 제어
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <time.h>

#include "candela.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "net.h"
#include "schedule.h"
#include "web.h"

namespace {

constexpr uint32_t STAT_LOG_MS = 600000;   // 주기 상태 요약 로그 (10분). 즉시 확인은 시리얼 info 또는 웹

String   g_line;
uint32_t g_lastReport = 0;
bool     g_otaStarted = false;

// 버튼 상태
bool     g_btnRaw    = true;   // HIGH = 안 누름
bool     g_btnDown   = false;
bool     g_btnLong   = false;
uint32_t g_btnChange = 0;
uint32_t g_btnAt     = 0;

void printStatus() {
    candela::Status s = candela::status();
    logf("STAT", "ble=%s/%s paired=%d auth=%d pwr=%d bri=%d rssi=%d conn=%lu drop=%lu", candela::linkName(s.link),
         candela::badge(), s.paired, s.authed, s.power, s.brightness, s.rssi, (unsigned long)s.connects,
         (unsigned long)s.drops);
    bool     isOn;
    uint16_t next;
    String   nextStr = schedule::nextEvent(isOn, next) ? String(isOn ? "ON " : "OFF ") + schedule::fmtHM(next) : "-";
    logf("STAT", "wifi=%d ip=%s rssi=%d time=%s next=%s last=%s heap=%u", net::wifiConnected(), net::ip().c_str(),
         net::rssi(), net::timeValid() ? schedule::fmtHM(schedule::nowMinute()).c_str() : "--:--", nextStr.c_str(),
         schedule::lastAction().c_str(), (unsigned)ESP.getFreeHeap());
}

void handleSchedCommand(const String& arg) {
    schedule::Config c = schedule::get();
    uint16_t         m;
    if (arg.isEmpty()) {
        logf("SCHED", "%s ON %s OFF %s 밝기 %u", c.enabled ? "활성" : "비활성", schedule::fmtHM(c.onMin).c_str(),
             schedule::fmtHM(c.offMin).c_str(), c.onBrightness);
        return;
    }
    if (arg.startsWith("on ") && schedule::parseHM(arg.substring(3), m)) c.onMin = m;
    else if (arg.startsWith("off ") && schedule::parseHM(arg.substring(4), m)) c.offMin = m;
    else if (arg.startsWith("bri ")) c.onBrightness = arg.substring(4).toInt();
    else if (arg == "enable") c.enabled = true;
    else if (arg == "disable") c.enabled = false;
    else {
        logf("CMD", "sched [on HH:MM | off HH:MM | bri 0-100 | enable | disable]");
        return;
    }
    if (!schedule::set(c)) logf("SCHED", "설정 거부 (ON과 OFF 동일 또는 범위 초과)");
}

void handleCommand(String cmd) {
    cmd.trim();
    cmd.toLowerCase();
    if (cmd.isEmpty()) return;
    logf("CMD", "%s", cmd.c_str());

    if (cmd == "on") candela::setPower(true);
    else if (cmd == "off") candela::setPower(false);
    else if (cmd == "toggle") candela::togglePower();
    else if (cmd.startsWith("b ")) candela::setBrightness(cmd.substring(2).toInt());
    else if (cmd == "pair") candela::startPairing();
    else if (cmd == "status") candela::requestStatus();
    else if (cmd == "info") printStatus();
    else if (cmd == "scan") candela::rescan();
    else if (cmd == "forget") candela::forgetPairing();
    else if (cmd == "sched") handleSchedCommand("");
    else if (cmd.startsWith("sched ")) handleSchedCommand(cmd.substring(6));
    else if (cmd == "reboot") ESP.restart();
    else logf("CMD", "명령: on | off | toggle | b <1-100> | pair | status | info | scan | forget | sched ... | reboot");
}

void pollSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            handleCommand(g_line);
            g_line = "";
        } else if (g_line.length() < 64) {
            g_line += c;
        }
    }
}

// BOOT 버튼: 짧게 = 수동 토글, BUTTON_LONG_MS 유지 = 페어링 시작 (유지 시점에 즉시 실행)
void pollButton() {
    uint32_t now = millis();
    bool     raw = digitalRead(BUTTON_PIN);
    if (raw != g_btnRaw) {
        g_btnRaw    = raw;
        g_btnChange = now;
    }
    if (now - g_btnChange >= BUTTON_DEBOUNCE_MS) {
        bool down = !g_btnRaw;
        if (down && !g_btnDown) {
            g_btnDown = true;
            g_btnLong = false;
            g_btnAt   = now;
        } else if (!down && g_btnDown) {
            g_btnDown = false;
            if (!g_btnLong) {
                logf("BTN", "짧게 누름 -> 토글");
                candela::togglePower();
            }
        }
    }
    if (g_btnDown && !g_btnLong && now - g_btnAt >= BUTTON_LONG_MS) {
        g_btnLong = true;
        logf("BTN", "길게 누름 -> 페어링 시작");
        candela::startPairing();
    }
    display::setHoldMs(g_btnDown && !g_btnLong ? now - g_btnAt : 0);
}

void startOta() {
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([] {
        logf("OTA", "업로드 시작");
        display::setOtaProgress(0);
        display::loop();
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        static int lastPct = -1;
        int        pct     = total ? (int)(done * 100ULL / total) : 0;
        if (pct / 10 != lastPct / 10) logf("OTA", "%d%%", pct);
        lastPct = pct;
        display::setOtaProgress(pct);
        display::loop();
    });
    ArduinoOTA.onEnd([] {
        logf("OTA", "완료, 재부팅");
        display::setOtaProgress(100);
        display::loop();
    });
    ArduinoOTA.onError([](ota_error_t e) {
        logf("OTA", "오류 %u", (unsigned)e);
        display::setOtaProgress(-1);
    });
    ArduinoOTA.begin();
    logf("OTA", "대기 %s.local:3232", HOSTNAME);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // USB 호스트 미연결 시 출력 대기 방지
    delay(1500);                // USB CDC 열림 대기
    logBegin();
    net::initClock();
    logf("SYS", "부팅 (reset reason %d), %s %s", (int)esp_reset_reason(), __DATE__, __TIME__);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    net::begin();          // (1) WiFi -> (2) NTP
    schedule::begin();
    display::begin();      // (3) OLED
    candela::begin();      // (4) NimBLE
}

void loop() {
    pollSerial();
    pollButton();
    net::loop();

    if (!g_otaStarted && net::wifiConnected()) {
        g_otaStarted = true;
        startOta();      // MDNS 시작 포함
        web::begin();
    }
    if (g_otaStarted) ArduinoOTA.handle();
    web::loop();

    schedule::loop();
    display::loop();

    if (millis() - g_lastReport >= STAT_LOG_MS) {
        g_lastReport = millis();
        printStatus();
    }
    delay(5);
}
