#include "web.h"

#include <ESPmDNS.h>
#include <WebServer.h>
#include <time.h>

#include "candela.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "schedule.h"
#include "web_page.h"

namespace {

WebServer g_srv(80);
bool      g_started  = false;
uint32_t  g_rebootAt = 0;

String esc(const String& s) {
    String o;
    o.reserve(s.length() + 2);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if ((uint8_t)c < 0x20) {
            o += ' ';
        } else {
            o += c;
        }
    }
    return o;
}

String statusJson() {
    candela::Status  b = candela::status();
    schedule::Config c = schedule::get();
    bool             tv = net::timeValid();

    char   timeBuf[8] = "--:--", ntpBuf[24] = "";
    time_t now        = time(nullptr);
    if (tv) {
        struct tm t;
        localtime_r(&now, &t);
        strftime(timeBuf, sizeof timeBuf, "%H:%M", &t);
    }
    if (time_t ls = net::lastSync()) {
        struct tm t;
        localtime_r(&ls, &t);
        strftime(ntpBuf, sizeof ntpBuf, "%m/%d %H:%M", &t);
    }
    bool     isOn;
    uint16_t nextMin;
    String   next = schedule::nextEvent(isOn, nextMin) ? String(isOn ? "ON " : "OFF ") + schedule::fmtHM(nextMin) : "";

    String j;
    j.reserve(720);
    j += "{\"time\":\"";
    j += timeBuf;
    j += "\",\"timeValid\":";
    j += tv ? "true" : "false";
    j += ",\"ntp\":\"";
    j += ntpBuf;
    j += "\",\"uptimeMs\":";
    j += millis();
    j += ",\"heap\":";
    j += ESP.getFreeHeap();

    j += ",\"wifi\":{\"ip\":\"";
    j += net::ip();
    j += "\",\"rssi\":";
    j += net::rssi();
    j += "}";

    j += ",\"ble\":{\"link\":\"";
    j += candela::linkName(b.link);
    j += "\",\"badge\":\"";
    j += candela::badge();
    j += "\",\"paired\":";
    j += b.paired ? "true" : "false";
    j += ",\"authed\":";
    j += b.authed ? "true" : "false";
    j += ",\"pairing\":";
    j += b.pairing ? "true" : "false";
    j += ",\"power\":";
    j += b.power;
    j += ",\"brightness\":";
    j += b.brightness;
    j += ",\"pendingPower\":";
    j += b.pendingPower;
    j += ",\"pendingBrightness\":";
    j += b.pendingBrightness;
    j += ",\"rssi\":";
    j += b.rssi;
    j += ",\"addr\":\"";
    j += b.haveAddr ? b.addr : "-";
    j += "\",\"connects\":";
    j += b.connects;
    j += ",\"drops\":";
    j += b.drops;
    j += ",\"connectedMs\":";
    j += b.connectedMs;
    j += ",\"retryMs\":";
    j += b.nextRetryMs;
    j += "}";

    j += ",\"schedule\":{\"enabled\":";
    j += c.enabled ? "true" : "false";
    j += ",\"on\":\"";
    j += schedule::fmtHM(c.onMin);
    j += "\",\"off\":\"";
    j += schedule::fmtHM(c.offMin);
    j += "\",\"onBrightness\":";
    j += c.onBrightness;
    j += ",\"next\":\"";
    j += next;
    j += "\",\"last\":\"";
    j += esc(schedule::lastAction());
    j += "\"}}";
    return j;
}

void sendStatus() {
    g_srv.sendHeader("Cache-Control", "no-store");
    g_srv.send(200, "application/json", statusJson());
}

void badRequest(const char* msg) { g_srv.send(400, "text/plain; charset=utf-8", msg); }

void handlePower() {
    String s = g_srv.arg("state");
    if (s == "on") candela::setPower(true);
    else if (s == "off") candela::setPower(false);
    else if (s == "toggle") candela::togglePower();
    else return badRequest("state=on|off|toggle");
    logf("WEB", "power %s", s.c_str());
    sendStatus();
}

void handleBrightness() {
    int v = g_srv.arg("value").toInt();
    if (v < 1 || v > 100) return badRequest("value=1-100");
    candela::setBrightness(v);
    logf("WEB", "brightness %d", v);
    sendStatus();
}

void handleSchedule() {
    schedule::Config c = schedule::get();
    if (g_srv.hasArg("enabled")) c.enabled = g_srv.arg("enabled") == "1" || g_srv.arg("enabled") == "true";
    uint16_t m;
    if (g_srv.hasArg("on")) {
        if (!schedule::parseHM(g_srv.arg("on"), m)) return badRequest("on=HH:MM");
        c.onMin = m;
    }
    if (g_srv.hasArg("off")) {
        if (!schedule::parseHM(g_srv.arg("off"), m)) return badRequest("off=HH:MM");
        c.offMin = m;
    }
    if (g_srv.hasArg("bri")) {
        int v = g_srv.arg("bri").toInt();
        if (v < 0 || v > 100) return badRequest("bri=0-100");
        c.onBrightness = v;
    }
    if (!schedule::set(c)) return badRequest("ON과 OFF 시각이 같거나 범위 초과");
    logf("WEB", "schedule 저장");
    sendStatus();
}

void handlePair() {
    logf("WEB", "pair");
    candela::startPairing();
    sendStatus();
}

void handleRefresh() {
    candela::requestStatus();
    sendStatus();
}

void handleReboot() {
    logf("WEB", "reboot 요청");
    sendStatus();
    g_rebootAt = millis() + 500;   // 응답 전송 후 재부팅
}

void handleLog() {
    g_srv.sendHeader("Cache-Control", "no-store");
    g_srv.send(200, "text/plain; charset=utf-8", logDump());
}

}  // namespace

namespace web {

void begin() {
    g_srv.on("/", HTTP_GET, [] { g_srv.send(200, "text/html; charset=utf-8", WEB_PAGE); });
    g_srv.on("/api/status", HTTP_GET, sendStatus);
    g_srv.on("/api/log", HTTP_GET, handleLog);
    g_srv.on("/api/power", HTTP_POST, handlePower);
    g_srv.on("/api/brightness", HTTP_POST, handleBrightness);
    g_srv.on("/api/schedule", HTTP_POST, handleSchedule);
    g_srv.on("/api/pair", HTTP_POST, handlePair);
    g_srv.on("/api/refresh", HTTP_POST, handleRefresh);
    g_srv.on("/api/reboot", HTTP_POST, handleReboot);
    g_srv.onNotFound([] { g_srv.send(404, "text/plain", "not found"); });
    g_srv.begin();
    MDNS.addService("http", "tcp", 80);
    g_started = true;
    logf("WEB", "http://%s.local/ , http://%s/", HOSTNAME, net::ip().c_str());
}

void loop() {
    if (!g_started) return;
    g_srv.handleClient();
    if (g_rebootAt && (int32_t)(millis() - g_rebootAt) >= 0) ESP.restart();
}

}  // namespace web
