#include "schedule.h"

#include <Preferences.h>
#include <time.h>

#include "candela.h"
#include "config.h"
#include "log.h"
#include "net.h"

namespace {
Preferences      g_prefs;
schedule::Config g_cfg{};
bool             g_catchUpDone = false;
uint32_t         g_lastOnKey   = 0;   // 이벤트 발생 분(epoch 분). 같은 분 중복 방지
uint32_t         g_lastOffKey  = 0;
uint32_t         g_lastTick    = 0;
String           g_lastAction  = "-";

void record(const char* what) {
    time_t    now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[48];
    snprintf(buf, sizeof buf, "%02d/%02d %02d:%02d %s", t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, what);
    g_lastAction = buf;
}

void turnOn(const char* why) {
    candela::setPower(true);
    if (g_cfg.onBrightness > 0) candela::setBrightness(g_cfg.onBrightness);
    record(why);
    logf("SCHED", "%s -> ON%s", why, g_cfg.onBrightness ? (" (밝기 " + String(g_cfg.onBrightness) + ")").c_str() : "");
}

void turnOff(const char* why) {
    candela::setPower(false);
    record(why);
    logf("SCHED", "%s -> OFF", why);
}

// 구간 안이면 즉시 ON (부팅 직후, 스케줄 활성화/변경 시)
void catchUp(const char* why) {
    uint16_t m = schedule::nowMinute();
    if (m == 0xFFFF) return;
    if (schedule::inWindow(m)) {
        turnOn(why);
    } else {
        logf("SCHED", "%s: 현재 %s, ON 구간 밖", why, schedule::fmtHM(m).c_str());
    }
}

void save() {
    g_prefs.putBool("en", g_cfg.enabled);
    g_prefs.putUShort("on", g_cfg.onMin);
    g_prefs.putUShort("off", g_cfg.offMin);
    g_prefs.putUChar("bri", g_cfg.onBrightness);
}
}  // namespace

namespace schedule {

String fmtHM(uint16_t m) {
    char buf[8];
    snprintf(buf, sizeof buf, "%02u:%02u", (unsigned)(m / 60) % 100, (unsigned)(m % 60));
    return buf;
}

bool parseHM(const String& s, uint16_t& out) {
    int colon = s.indexOf(':');
    if (colon < 1) return false;
    int h = s.substring(0, colon).toInt();
    int m = s.substring(colon + 1).toInt();
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    out = h * 60 + m;
    return true;
}

uint16_t nowMinute() {
    if (!net::timeValid()) return 0xFFFF;
    time_t    now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_hour * 60 + t.tm_min;
}

void begin() {
    g_prefs.begin("sched", false);
    uint16_t defOn = 18 * 60, defOff = 23 * 60;
    parseHM(SCHED_ON_TIME, defOn);
    parseHM(SCHED_OFF_TIME, defOff);
    g_cfg.enabled      = g_prefs.getBool("en", SCHED_ENABLED);
    g_cfg.onMin        = g_prefs.getUShort("on", defOn);
    g_cfg.offMin       = g_prefs.getUShort("off", defOff);
    g_cfg.onBrightness = g_prefs.getUChar("bri", SCHED_ON_BRIGHTNESS);
    logf("SCHED", "%s ON %s OFF %s 밝기 %u (%s)", g_cfg.enabled ? "활성" : "비활성", fmtHM(g_cfg.onMin).c_str(),
         fmtHM(g_cfg.offMin).c_str(), g_cfg.onBrightness, g_prefs.isKey("on") ? "NVS" : "기본값");
}

bool inWindow(uint16_t m) {
    if (g_cfg.onMin == g_cfg.offMin) return false;
    if (g_cfg.onMin < g_cfg.offMin) return m >= g_cfg.onMin && m < g_cfg.offMin;
    return m >= g_cfg.onMin || m < g_cfg.offMin;   // 자정 넘김
}

bool nextEvent(bool& isOn, uint16_t& minute) {
    uint16_t m = nowMinute();
    if (!g_cfg.enabled || m == 0xFFFF) return false;
    // 현재 분 이후 가장 가까운 이벤트 (현재 분과 같으면 이미 처리된 것으로 간주)
    uint16_t dOn  = (g_cfg.onMin + 1440 - m - 1) % 1440 + 1;
    uint16_t dOff = (g_cfg.offMin + 1440 - m - 1) % 1440 + 1;
    isOn   = dOn <= dOff;
    minute = isOn ? g_cfg.onMin : g_cfg.offMin;
    return true;
}

Config get() { return g_cfg; }

bool set(const Config& c) {
    if (c.onMin >= 1440 || c.offMin >= 1440 || c.onMin == c.offMin || c.onBrightness > 100) return false;
    bool wasEnabled = g_cfg.enabled;
    bool changed    = c.onMin != g_cfg.onMin || c.offMin != g_cfg.offMin;
    g_cfg           = c;
    save();
    logf("SCHED", "설정 저장: %s ON %s OFF %s 밝기 %u", c.enabled ? "활성" : "비활성", fmtHM(c.onMin).c_str(),
         fmtHM(c.offMin).c_str(), c.onBrightness);
    if (c.enabled && (!wasEnabled || changed)) catchUp("스케줄 적용");
    return true;
}

void loop() {
    uint32_t ms = millis();
    if (ms - g_lastTick < 500) return;
    g_lastTick = ms;
    if (!net::timeValid()) return;

    if (!g_catchUpDone) {
        g_catchUpDone = true;
        if (g_cfg.enabled) catchUp("부팅 구간 판정");
    }
    if (!g_cfg.enabled) return;

    time_t   now = time(nullptr);
    uint32_t key = (uint32_t)(now / 60);
    uint16_t m   = nowMinute();

    if (m == g_cfg.onMin && g_lastOnKey != key) {
        g_lastOnKey = key;
        turnOn("예약 ON");
    }
    if (m == g_cfg.offMin && g_lastOffKey != key) {
        g_lastOffKey = key;
        turnOff("예약 OFF");
    }
}

String lastAction() { return g_lastAction; }

}  // namespace schedule
