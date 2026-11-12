#include "display.h"

#include <U8g2lib.h>
#include <time.h>

#include "candela.h"
#include "config.h"
#include "log.h"
#include "net.h"
#include "schedule.h"

namespace {

// 72x40 ER 드라이버. I2C 핀 미지정 시 C3 기본 SCL=GPIO9(BOOT 버튼)로 초기화되므로 명시.
U8G2_SSD1306_72X40_ER_F_HW_I2C g_oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);

constexpr int kW = 70;   // 이동 여유 2px 제외한 가용 폭

// 번인 방지 이동 패턴 (dx, dy), 연속 위치 간 1-2px 차이
const int8_t kShift[][2] = {{0, 0}, {1, 1}, {2, 0}, {2, 2}, {1, 0}, {0, 2}, {1, 2}, {2, 1}};

uint32_t g_lastDraw = 0;
char     g_msg1[24]  = "";
char     g_msg2[24]  = "";
uint32_t g_msgUntil  = 0;
uint32_t g_holdMs    = 0;
int      g_ota       = -1;

void drawCentered(const char* s, int y, int dx) {
    int w = g_oled.getStrWidth(s);
    int x = dx + (kW - w) / 2;
    g_oled.drawStr(x < 0 ? 0 : x, y, s);
}

void drawMessage(const char* l1, const char* l2, int dx, int dy) {
    g_oled.setFont(u8g2_font_6x10_tf);
    drawCentered(l1, dy + 13, dx);
    g_oled.setFont(u8g2_font_5x7_tf);
    if (g_oled.getStrWidth(l2) > kW) g_oled.setFont(u8g2_font_4x6_tf);
    drawCentered(l2, dy + 29, dx);
}

void drawMain(int dx, int dy) {
    char buf[20];
    bool tv = net::timeValid();

    // 4줄 배치 (글자 높이 13 + 6 + 6 + 5, 줄 간격 3, 2, 2 = 37px, 이동 여유 2px 포함 40px 이내)
    // (1) 현재 시각 (helvB14 숫자 높이 13px)
    g_oled.setFont(u8g2_font_helvB14_tn);
    if (tv) {
        time_t    now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        snprintf(buf, sizeof buf, "--:--");
    }
    drawCentered(buf, dy + 13, dx);

    // (2) 다음 예약
    g_oled.setFont(u8g2_font_5x7_tf);
    bool     isOn;
    uint16_t m;
    if (!tv) snprintf(buf, sizeof buf, "NO TIME");
    else if (schedule::nextEvent(isOn, m)) snprintf(buf, sizeof buf, "%s %s", isOn ? "ON" : "OFF", schedule::fmtHM(m).c_str());
    else snprintf(buf, sizeof buf, "SCHED OFF");
    drawCentered(buf, dy + 22, dx);

    // (3) BLE 상태 + 램프 전원 표시 (채운 원 = 켜짐, 빈 원 = 꺼짐)
    snprintf(buf, sizeof buf, "BLE %s", candela::badge());
    g_oled.drawStr(dx + 1, dy + 30, buf);
    candela::Status s = candela::status();
    if (s.power == 1) g_oled.drawDisc(dx + 64, dy + 27, 3);
    else if (s.power == 0) g_oled.drawCircle(dx + 64, dy + 27, 3);

    // (4) 현재 IP (4x6, "192.168.xxx.xxx" 최대 60px)
    g_oled.setFont(u8g2_font_4x6_tf);
    drawCentered(net::wifiConnected() ? net::ip().c_str() : "NO WIFI", dy + 37, dx);
}

}  // namespace

namespace display {

void begin() {
    g_oled.begin();
    g_oled.getU8x8()->x_offset = OLED_X_OFFSET;
    g_oled.setContrast(OLED_CONTRAST);
    g_oled.clearBuffer();
    g_oled.setFont(u8g2_font_6x10_tf);
    g_oled.drawStr(4, 13, "CANDELA");
    g_oled.setFont(u8g2_font_5x7_tf);
    g_oled.drawStr(4, 29, "booting...");
    g_oled.sendBuffer();
    logf("OLED", "초기화 72x40, contrast %u, x_offset %u", OLED_CONTRAST, OLED_X_OFFSET);
}

void showMessage(const char* l1, const char* l2, uint32_t ms) {
    snprintf(g_msg1, sizeof g_msg1, "%s", l1);
    snprintf(g_msg2, sizeof g_msg2, "%s", l2);
    g_msgUntil = millis() + ms;
    g_lastDraw = 0;   // 즉시 갱신
}

void setHoldMs(uint32_t ms) {
    if ((ms == 0) != (g_holdMs == 0)) g_lastDraw = 0;
    g_holdMs = ms;
}

void setOtaProgress(int pct) {
    // 시작, 종료 시점만 즉시 갱신. 진행 중에는 OLED_REFRESH_MS 주기 유지 (I2C 전송으로 OTA 지연 방지)
    if (pct <= 0 || pct >= 100) g_lastDraw = 0;
    g_ota = pct;
}

void loop() {
    uint32_t now = millis();
    if (g_lastDraw && now - g_lastDraw < OLED_REFRESH_MS) return;
    g_lastDraw = now ? now : 1;

    const int8_t* sh = kShift[(now / OLED_SHIFT_MS) % (sizeof kShift / sizeof kShift[0])];
    int           dx = sh[0], dy = sh[1];

    g_oled.clearBuffer();
    char buf[24];
    if (g_ota >= 0) {
        snprintf(buf, sizeof buf, "%d%%", g_ota);
        drawMessage("OTA", buf, dx, dy);
        g_oled.drawFrame(dx + 4, dy + 33, 62, 5);
        g_oled.drawBox(dx + 5, dy + 34, 60 * g_ota / 100, 3);
    } else if (g_holdMs >= 500 && g_holdMs < BUTTON_LONG_MS) {
        snprintf(buf, sizeof buf, "PAIR IN %lus", (unsigned long)((BUTTON_LONG_MS - g_holdMs + 999) / 1000));
        drawMessage("HOLD", buf, dx, dy);
    } else if (candela::status().pairing) {
        drawMessage("PAIRING", "TURN THE RING", dx, dy);
    } else if ((int32_t)(g_msgUntil - now) > 0) {
        drawMessage(g_msg1, g_msg2, dx, dy);
    } else {
        drawMain(dx, dy);
    }
    g_oled.sendBuffer();
}

}  // namespace display
