#include "log.h"

#include <stdarg.h>
#include <time.h>

namespace {
constexpr int kLines   = 60;
constexpr int kLineLen = 128;

char              g_lines[kLines][kLineLen];
int               g_head  = 0;
int               g_count = 0;
SemaphoreHandle_t g_mtx   = nullptr;
}  // namespace

void logBegin() {
    if (!g_mtx) g_mtx = xSemaphoreCreateMutex();
}

void logf(const char* tag, const char* fmt, ...) {
    char msg[104];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    // NTP 동기화 전에는 부팅 후 경과 초로 표기
    char   ts[16];
    time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm t;
        localtime_r(&now, &t);
        strftime(ts, sizeof ts, "%H:%M:%S", &t);
    } else {
        snprintf(ts, sizeof ts, "+%lus", (unsigned long)(millis() / 1000));
    }

    if (g_mtx) xSemaphoreTake(g_mtx, portMAX_DELAY);
    snprintf(g_lines[g_head], kLineLen, "%s [%s] %s", ts, tag, msg);
    Serial.println(g_lines[g_head]);
    g_head = (g_head + 1) % kLines;
    if (g_count < kLines) g_count++;
    if (g_mtx) xSemaphoreGive(g_mtx);
}

String logDump() {
    String out;
    if (g_mtx) xSemaphoreTake(g_mtx, portMAX_DELAY);
    out.reserve(g_count * 64);
    int start = (g_head - g_count + kLines) % kLines;
    for (int i = 0; i < g_count; i++) {
        out += g_lines[(start + i) % kLines];
        out += '\n';
    }
    if (g_mtx) xSemaphoreGive(g_mtx);
    return out;
}
