#include "candela.h"

#include <NimBLEDevice.h>
#include <Preferences.h>

#include "config.h"
#include "log.h"

using candela::Link;

namespace {

const NimBLEUUID kSvc(CANDELA_SVC_UUID);
const NimBLEUUID kCtrl(CANDELA_CTRL_UUID);
const NimBLEUUID kNotify(CANDELA_NOTIFY_UUID);

NimBLEClient*               g_client = nullptr;
NimBLERemoteCharacteristic* g_ctrl   = nullptr;
NimBLEAddress               g_addr;
bool                        g_haveAddr = false;
Preferences                 g_prefs;

// 상태 (BLE 태스크, NimBLE 호스트 태스크에서 갱신)
volatile Link    g_link       = Link::Disconnected;
volatile int8_t  g_power      = -1;
volatile int16_t g_bright     = -1;
volatile int8_t  g_rssi       = 0;
volatile bool    g_paired     = false;
volatile bool    g_authed     = false;
volatile bool    g_pairing    = false;
volatile bool    g_dropped    = false;
volatile bool    g_savePaired = false;
volatile int     g_dropReason = 0;
volatile bool    g_logStatusRx = false;   // 다음 상태 응답을 변화 없어도 로그 (명시 요청, 연결 직후)

// 요청 (외부 API에서 설정, BLE 태스크에서 소비)
volatile int8_t  g_wantPower  = -1;
volatile int16_t g_wantBright = -1;
volatile bool    g_reqPair    = false;
volatile bool    g_reqStatus  = false;
volatile bool    g_reqRescan  = false;
volatile bool    g_reqForget  = false;
volatile bool    g_kick       = false;   // 백오프 무시하고 즉시 연결 시도

uint32_t g_backoff    = BLE_BACKOFF_MIN_MS;
uint32_t g_nextTry    = 0;
uint32_t g_connAt     = 0;
uint32_t g_lastStatus = 0;
uint32_t g_statusDue  = 0;   // 명령 직후 상태 확인 예약 시각 (0 = 없음)
uint32_t g_pairAt     = 0;
uint32_t g_nextCmdAt  = 0;
uint32_t g_connects   = 0;
uint32_t g_drops      = 0;

void hexify(const uint8_t* d, size_t n, char* out, size_t outLen) {
    size_t pos = 0;
    for (size_t i = 0; i < n && pos + 3 < outLen; i++) {
        pos += snprintf(out + pos, outLen - pos, i ? " %02X" : "%02X", d[i]);
    }
    out[pos] = '\0';
}

// quiet = true: 성공 시 로그 생략 (주기 상태 요청 등 반복 전송용). 실패는 항상 로그.
bool sendCmd(std::initializer_list<uint8_t> bytes, const char* what, bool quiet = false) {
    if (!g_client || !g_client->isConnected() || !g_ctrl) return false;
    uint8_t buf[CANDELA_CMD_LEN] = {0};
    size_t  i = 0;
    for (uint8_t b : bytes) {
        if (i < sizeof buf) buf[i++] = b;
    }
    bool withResp = g_ctrl->canWrite();
    bool ok       = g_ctrl->writeValue(buf, sizeof buf, withResp);
    if (quiet && ok) return ok;
    char hex[16];
    hexify(buf, i, hex, sizeof hex);
    logf("BLE", "TX %-6s [%s] %s", what, hex, ok ? "ok" : "FAIL");
    return ok;
}

// 컨트롤러가 측정값 없음(-127) 또는 실패(0)를 반환하면 이전 값 유지
void updateRssi() {
    int r = g_client->getRssi();
    if (r < 0 && r > -127) g_rssi = r;
}

void onNotify(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
    char hex[64];
    hexify(d, n < 18 ? n : 18, hex, sizeof hex);

    if (n >= 4 && d[0] == 0x43 && d[1] == 0x45) {
        int8_t  pwr     = d[2] == 0x01 ? 1 : (d[2] == 0x02 ? 0 : -1);
        int16_t bri     = d[3];
        bool    changed = pwr != g_power || bri != g_bright;
        g_power  = pwr;
        g_bright = bri;
        // 주기 상태 응답은 변화가 있을 때만 로그 (로그 링버퍼 보존)
        if (changed || g_logStatusRx) {
            g_logStatusRx = false;
            logf("BLE", "RX 상태 power=%s bright=%d [%s]", pwr == 1 ? "ON" : (pwr == 0 ? "OFF" : "?"), bri, hex);
        }
        return;
    }
    if (n >= 3 && d[0] == 0x43 && d[1] == 0x63) {
        switch (d[2]) {
            case 0x01:
                g_pairing = true;
                g_pairAt  = millis();
                logf("BLE", "RX 페어링 모드 진입: 램프 링을 돌려 확인 필요");
                break;
            case 0x02:
                g_pairing = false;
                g_authed  = true;
                if (!g_paired) g_savePaired = true;
                logf("BLE", "RX 페어링 완료(인증 OK)");
                break;
            default:
                logf("BLE", "RX 페어링 응답 0x%02X [%s]", d[2], hex);
                break;
        }
        return;
    }
    logf("BLE", "RX [%s]", hex);
}

class ClientCb : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* c) override {
        logf("BLE", "GAP 연결 %s", c->getPeerAddress().toString().c_str());
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        g_dropReason = reason;
        g_dropped    = true;
    }
};
ClientCb g_cb;

void loadAddr() {
    String mac  = LAMP_MAC;
    uint8_t typ = BLE_ADDR_PUBLIC;
    if (mac.isEmpty()) {
        mac = g_prefs.getString("mac", "");
        typ = g_prefs.getUChar("mactype", BLE_ADDR_PUBLIC);
    }
    if (mac.isEmpty()) return;
    g_addr     = NimBLEAddress(std::string(mac.c_str()), typ);
    g_haveAddr = true;
    logf("BLE", "램프 주소 %s (type %u, %s)", g_addr.toString().c_str(), typ,
         String(LAMP_MAC).isEmpty() ? "NVS" : "secrets.h");
}

bool scanForLamp() {
    g_link = Link::Scanning;
    logf("BLE", "스캔 시작 %lums, 대상 '%s' 또는 서비스 FE87", (unsigned long)BLE_SCAN_MS, LAMP_NAME);
    NimBLEScan* s = NimBLEDevice::getScan();
    s->setActiveScan(true);
    s->setInterval(100);
    s->setWindow(99);
    s->setDuplicateFilter(1);
    NimBLEScanResults r = s->getResults(BLE_SCAN_MS, false);

    bool found = false;
    int  named = 0;
    for (int i = 0; i < r.getCount(); i++) {
        const NimBLEAdvertisedDevice* d = r.getDevice(i);
        std::string name = d->haveName() ? d->getName() : "";
        bool        fe87 = d->isAdvertisingService(kSvc);
        if (name.empty() && !fe87) continue;
        named++;
        logf("SCAN", "%s t=%u rssi=%d '%s'%s", d->getAddress().toString().c_str(), d->getAddressType(),
             d->getRSSI(), name.c_str(), fe87 ? " [FE87]" : "");
        if (!found && (name == LAMP_NAME || fe87)) {
            g_addr     = d->getAddress();
            g_rssi     = d->getRSSI();
            g_haveAddr = true;
            found      = true;
        }
    }
    logf("BLE", "스캔 완료: 전체 %d, 이름/FE87 %d", r.getCount(), named);
    s->clearResults();
    g_link = Link::Disconnected;

    if (found) {
        g_prefs.putString("mac", g_addr.toString().c_str());
        g_prefs.putUChar("mactype", g_addr.getType());
        logf("BLE", "램프 발견 %s (type %u), NVS 저장", g_addr.toString().c_str(), g_addr.getType());
    } else {
        logf("BLE", "램프 미발견");
    }
    return found;
}

bool connectLamp() {
    g_link    = Link::Connecting;
    g_dropped = false;
    logf("BLE", "연결 시도 %s", g_addr.toString().c_str());
    if (!g_client->connect(g_addr, true, false, true)) {
        logf("BLE", "연결 실패 err=%d", g_client->getLastError());
        return false;
    }

    g_ctrl                          = nullptr;
    NimBLERemoteCharacteristic* ntf = nullptr;
    if (NimBLERemoteService* svc = g_client->getService(kSvc)) {
        g_ctrl = svc->getCharacteristic(kCtrl);
        ntf    = svc->getCharacteristic(kNotify);
    }
    if (!g_ctrl || !ntf) {
        for (NimBLERemoteService* s : g_client->getServices(true)) {
            if (!g_ctrl) g_ctrl = s->getCharacteristic(kCtrl);
            if (!ntf) ntf = s->getCharacteristic(kNotify);
        }
    }
    if (!g_ctrl || !ntf) {
        logf("BLE", "캐릭터리스틱 미발견 ctrl=%d notify=%d", g_ctrl != nullptr, ntf != nullptr);
        g_ctrl = nullptr;
        g_client->disconnect();
        return false;
    }
    if (!ntf->subscribe(true, onNotify, true)) {
        logf("BLE", "Notify 구독 실패");
        g_ctrl = nullptr;
        g_client->disconnect();
        return false;
    }

    g_link    = Link::Connected;
    g_authed  = false;
    g_pairing = false;
    g_connAt  = millis();
    g_connects++;
    g_logStatusRx = true;
    updateRssi();
    logf("BLE", "준비 완료 (rssi %d, MTU %u, ctrl %s)", g_rssi, g_client->getMTU(),
         g_ctrl->canWrite() ? "write" : "write-no-rsp");
    return true;
}

void scheduleRetry(uint32_t now) {
    g_nextTry = now + g_backoff;
    logf("BLE", "%lus 후 재연결", (unsigned long)(g_backoff / 1000));
    g_backoff = min<uint32_t>(g_backoff * 2, BLE_BACKOFF_MAX_MS);
}

void bleTask(void*) {
    for (;;) {
        uint32_t now = millis();

        if (g_dropped) {
            g_dropped = false;
            g_drops++;
            uint32_t lived = g_link == Link::Connected ? now - g_connAt : 0;
            g_link    = Link::Disconnected;
            g_ctrl    = nullptr;
            g_authed  = false;
            if (g_pairing) g_reqPair = true;   // 페어링 중 끊김: 재연결 후 페어링 요청 재전송
            logf("BLE", "연결 끊김 reason=0x%X, 유지 %lus", g_dropReason, (unsigned long)(lived / 1000));
            if (lived > 60000) g_backoff = BLE_BACKOFF_MIN_MS;
            scheduleRetry(now);
        }

        if (g_savePaired) {
            g_savePaired = false;
            g_paired     = true;
            g_prefs.putBool("paired", true);
            logf("BLE", "페어링 이력 NVS 저장");
        }
        if (g_reqForget) {
            g_reqForget = false;
            g_paired    = false;
            g_prefs.putBool("paired", false);
            logf("BLE", "페어링 이력 삭제");
        }
        if (g_reqRescan) {
            g_reqRescan = false;
            if (g_client->isConnected()) g_client->disconnect();
            g_prefs.remove("mac");
            g_prefs.remove("mactype");
            g_haveAddr = String(LAMP_MAC).length() > 0;
            g_kick     = true;
            logf("BLE", "저장 주소 삭제, 재탐색");
        }
        if (g_kick) {
            g_kick    = false;
            g_backoff = BLE_BACKOFF_MIN_MS;
            g_nextTry = now;
        }

        bool connected = g_client->isConnected() && g_ctrl != nullptr && g_link == Link::Connected;

        if (!connected) {
            if (g_link != Link::Connecting && (int32_t)(now - g_nextTry) >= 0) {
                bool ok = (g_haveAddr || scanForLamp()) && connectLamp();
                if (ok) {
                    if (g_reqPair) {
                        g_reqPair = false;
                        g_pairing = true;
                        sendCmd({0x43, 0x67, 0x02}, "pair");
                    } else if (g_paired) {
                        // 페어링 이력 있음: 연결마다 인증 요청 (램프가 기억하면 즉시 43 63 02 응답)
                        sendCmd({0x43, 0x67, 0x02}, "auth");
                    }
                    g_statusDue = millis() + 300;
                } else {
                    g_link = Link::Disconnected;
                    if (g_client->isConnected()) g_client->disconnect();
                    g_dropped = false;
                    scheduleRetry(millis());
                }
            }
        } else {
            if (g_reqPair) {
                g_reqPair = false;
                g_pairing = true;
                sendCmd({0x43, 0x67, 0x02}, "pair");
            }
            if (g_wantPower >= 0 && (int32_t)(now - g_nextCmdAt) >= 0) {
                int8_t p = g_wantPower;
                if (sendCmd({0x43, 0x40, (uint8_t)(p ? 0x01 : 0x02)}, p ? "on" : "off")) {
                    if (g_wantPower == p) g_wantPower = -1;
                    g_power     = p;
                    g_statusDue = now + 400;
                } else {
                    g_nextCmdAt = now + 1000;
                }
            }
            if (g_wantBright >= 0 && (int32_t)(now - g_nextCmdAt) >= 0) {
                int16_t b = g_wantBright;
                if (sendCmd({0x43, 0x42, (uint8_t)b}, "bright")) {
                    if (g_wantBright == b) g_wantBright = -1;
                    g_bright    = b;
                    g_statusDue = now + 400;
                } else {
                    g_nextCmdAt = now + 1000;
                }
            }
            bool due = g_statusDue && (int32_t)(now - g_statusDue) >= 0;
            if (g_reqStatus || due || now - g_lastStatus >= BLE_STATUS_POLL_MS) {
                bool explicitReq = g_reqStatus;
                g_reqStatus      = false;
                g_statusDue      = 0;
                g_lastStatus     = now;
                if (explicitReq) g_logStatusRx = true;   // 응답이 write 완료 전에 올 수 있어 전송 전에 설정
                sendCmd({0x43, 0x44}, "status", !explicitReq);
                updateRssi();
            }
        }

        // g_pairAt은 다른 태스크에서 now 이후 값으로 갱신될 수 있으므로 부호 있는 비교
        if ((g_pairing || g_reqPair) && (int32_t)(now - g_pairAt) > (int32_t)BLE_PAIR_TIMEOUT_MS) {
            g_pairing = false;
            g_reqPair = false;
            logf("BLE", "페어링 대기 시간 초과");
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

}  // namespace

namespace candela {

void begin() {
    g_prefs.begin("candela", false);
    g_paired = g_prefs.getBool("paired", false);

    NimBLEDevice::init("candela-timer");
    NimBLEDevice::setPower(9);
    g_client = NimBLEDevice::createClient();
    g_client->setClientCallbacks(&g_cb, false);
    g_client->setConnectTimeout(BLE_CONNECT_TIMEOUT_MS);

    logf("BLE", "NimBLE 초기화, 자체 주소 %s, 페어링 이력 %s", NimBLEDevice::getAddress().toString().c_str(),
         g_paired ? "있음" : "없음");
    loadAddr();
    xTaskCreate(bleTask, "ble", 8192, nullptr, 2, nullptr);
}

void setPower(bool on) { g_wantPower = on ? 1 : 0; }

void togglePower() {
    int8_t cur = g_wantPower >= 0 ? g_wantPower : g_power;
    setPower(cur != 1);
}

void setBrightness(uint8_t pct) { g_wantBright = constrain(pct, 1, 100); }
void requestStatus() { g_reqStatus = true; }

void startPairing() {
    g_pairAt  = millis();
    g_reqPair = true;
    g_kick    = true;
    logf("BLE", "페어링 요청 접수");
}

void forgetPairing() { g_reqForget = true; }
void rescan() { g_reqRescan = true; }

bool isConnected() { return g_link == Link::Connected; }

Status status() {
    Status s{};
    uint32_t now   = millis();
    s.link         = g_link;
    s.authed       = g_authed;
    s.paired       = g_paired;
    s.pairing      = g_pairing || g_reqPair;
    s.power        = g_power;
    s.brightness   = g_bright;
    s.rssi         = g_rssi;
    s.haveAddr     = g_haveAddr;
    s.connects     = g_connects;
    s.drops        = g_drops;
    s.connectedMs  = g_link == Link::Connected ? now - g_connAt : 0;
    s.nextRetryMs  = (g_link == Link::Disconnected && (int32_t)(g_nextTry - now) > 0) ? g_nextTry - now : 0;
    s.pendingPower      = g_wantPower;
    s.pendingBrightness = g_wantBright;
    if (g_haveAddr) {
        snprintf(s.addr, sizeof s.addr, "%s", g_addr.toString().c_str());
    }
    return s;
}

const char* linkName(Link l) {
    switch (l) {
        case Link::Disconnected: return "disconnected";
        case Link::Scanning:     return "scanning";
        case Link::Connecting:   return "connecting";
        case Link::Connected:    return "connected";
    }
    return "?";
}

const char* badge() {
    if (g_pairing || g_reqPair) return "PAIR";
    return g_link == Link::Connected ? "OK" : "X";
}

}  // namespace candela
