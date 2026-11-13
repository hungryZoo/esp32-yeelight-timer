# Yeelight Candela BLE 타이머 (ESP32-C3)

Yeelight Candela(BLE 전용 램프)를 정해진 시각에 켜고 끄는 단독 타이머 장치. Home Assistant 없이 ESP32-C3 단독 동작. WiFi 웹페이지로 전원, 밝기, 스케줄, 페어링 제어.

## 하드웨어
- 보드: ESP32-C3 0.42" OLED 내장 개발보드 (USB-Serial-JTAG 네이티브 USB, 별도 UART 칩 없음, 4MB flash)
- OLED: SSD1306 72x40, I2C SDA=GPIO5, SCL=GPIO6. U8g2 `72X40_ER` 드라이버 사용 (x 오프셋 28 내장, `OLED_X_OFFSET`로 조정)
- BOOT 버튼: GPIO9 (짧게 = 수동 토글, 3초 유지 = 페어링)
- 시리얼 포트: `/dev/cu.usbmodem101` (Espressif USB JTAG/serial debug unit). BOOT 버튼 없이 자동 다운로드 모드 진입
- 램프: public 주소, 광고 이름 `yeelight_ms` (ESP32 스캔 실측. iOS LightBlue 목록 표시 이름은 `yl_candela`), 광고 서비스 FE87. 주소 고정은 `secrets.h`의 `LAMP_MAC` (미지정 시 스캔 후 NVS 저장)
- 네트워크: DHCP, mDNS `candela-timer.local`
- 장치별 값(램프 주소, IP)은 `CLAUDE.local.md` (gitignore)

## 소스 구성
| 파일 | 역할 |
|---|---|
| `include/config.h` | 스케줄 기본값(최상단), 네트워크, 램프 이름, UUID, 타이밍, 핀 상수. `secrets.h` 포함 |
| `include/secrets.h` | WiFi, OTA 비밀번호, 선택 `LAMP_MAC` (gitignore). 템플릿 `include/secrets.h.example` |
| `src/candela.*` | Candela BLE 제어. 전용 FreeRTOS 태스크에서 연결, 백오프 재연결(2-60초), 인증, 명령 전송. 외부 API는 요청 플래그만 설정 |
| `src/net.*` | WiFi 연결 유지, SNTP(KST) |
| `src/schedule.*` | 매일 ON/OFF. 분 단위 이벤트 + 같은 분 중복 방지, 부팅 시 구간 판정, NVS 저장 |
| `src/display.*` | OLED 4줄 화면 (시각 helvB14, 다음 예약, BLE 상태 + 램프 전원 원, IP), 번인 방지 이동 |
| `src/web.*`, `src/web_page.h` | 웹서버 (포트 80) + 단일 HTML 제어 페이지 |
| `src/log.*` | 시리얼 로그 + 최근 60줄 링버퍼 (웹 `/api/log`) |
| `src/main.cpp` | setup/loop, 버튼, ArduinoOTA, 시리얼 명령 |
| `tools/serial_console.py` | 로그 수신 + 시각 지정 명령 전송 (`logs/`에 저장) |
| `tools/check_secrets.py` | pre 스크립트. `secrets.h` 없음 또는 예시 값이면 `/dev/tty`로 입력받아 작성, 터미널 없으면 안내 후 빌드 중단. `main.cpp` `static_assert`도 같은 규칙 |
| `tools/ota_auth.py` | OTA 업로드 시 `secrets.h`의 `OTA_PASSWORD`를 espota `--auth`로 전달 |

## 명령
- PlatformIO 설치: `uv tool install platformio` (`~/.local/bin/pio`)
- 컴파일: `pio run` (코드 수정 후 반드시 실행)
- USB 업로드: `pio run -t upload`
- OTA 업로드: `pio run -e ota -t upload` (mDNS 실패 시 `--upload-port <장치 IP>`)
- 로그: `timeout 15 pio device monitor` (무한 대기이므로 항상 timeout)
- 명령 전송 테스트: `uv run --with pyserial tools/serial_console.py -t 30 -c "3:info" -c "5:on"` (`--reset` 지정 시 칩 리셋 후 수신)

## 시리얼 명령
`on`, `off`, `toggle`, `b <1-100>`, `pair`, `status`, `info`, `scan`(저장 주소 삭제 후 재탐색), `forget`(페어링 이력 삭제), `sched`, `sched on HH:MM`, `sched off HH:MM`, `sched bri 0-100`, `sched enable|disable`, `reboot`

## 웹 API
| 메서드 | 경로 | 파라미터 |
|---|---|---|
| GET | `/` | 제어 페이지 |
| GET | `/api/status` | 상태 JSON (time, wifi, ble, schedule) |
| GET | `/api/log` | 최근 로그 텍스트 |
| POST | `/api/power` | `state=on\|off\|toggle` |
| POST | `/api/brightness` | `value=1-100` |
| POST | `/api/schedule` | `enabled=0\|1`, `on=HH:MM`, `off=HH:MM`, `bri=0-100` |
| POST | `/api/pair`, `/api/refresh`, `/api/reboot` | 없음 |

## Candela BLE 프로토콜 (실측 확인)
- GATT 서비스 FE87(16-bit). 제어 AA7D3F34-2D4F-41E0-807F-52FBF8CF7443 (Read, Write), 알림 8F65073D-9F57-4AAA-AFEA-397D19D5BBEB (Read, Notify). MTU 23
- 명령 18바이트(0x00 패딩): 켜기 `43 40 01`, 끄기 `43 40 02`, 밝기 `43 42 XX`, 페어링/인증 `43 67 02`, 상태 `43 44`
- 알림: `43 45 PP BB ..` 상태(PP 01=ON 02=OFF, BB 밝기), `43 63 01` 페어링 모드(램프 깜빡임), `43 63 02` 페어링 완료
- 램프는 명령마다 상태 알림(`43 45`)을 즉시 송신
- 페어링 전에도 명령은 동작하나 연결이 약 28초마다 끊김(reason 0x208, supervision timeout)
- 페어링 후 연결 유지. 이후 연결마다 `43 67 02` 전송 시 깜빡임 없이 즉시 `43 63 02` 응답 (펌웨어가 NVS `paired` 플래그로 자동 전송)
- OFF 상태에서 밝기 명령 전송 시 램프가 켜짐

## 운영 메모
- WiFi와 BLE 공존을 위해 modem sleep 유지 (`WiFi.setSleep(false)` 금지)
- 리셋 직후 WiFi가 IDLE(status 0)에 머무는 경우 있음 → 부팅 대기 중 7초 간격 재시도
- `candela-timer.local`은 IPv6(AAAA) 조회 무응답으로 macOS curl에서 이름 해석 3-5초 지연. `curl -4` 또는 IP 사용
- 소프트 리셋 후 RTC 시각 유지 → NTP 전에도 스케줄 동작. TZ는 setup 최초에 `KST-9` 설정
- 로그 링버퍼 보존: 30초 주기 상태 요청은 응답 값이 바뀔 때만 로그, STAT 요약은 10분 주기. BLE RSSI는 -127(측정값 없음)과 0(실패)을 무시하고 이전 값 유지
- pyserial로 포트 열 때 DTR을 먼저 내리면 칩 리셋 (RTS=1, DTR=0). `serial_console.py`는 DTR=1 유지로 회피

## 규칙
- 각 단계 컴파일 확인, 업로드, 로그 검증 후 다음 단계 진행
- 램프 페어링 시점은 사용자 물리 동작(링 회전)이 필요하므로 그 시점에 사용자에게 알림
