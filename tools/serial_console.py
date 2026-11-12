"""ESP32-C3 USB CDC 시리얼 로그 수신 + 시각 지정 명령 전송 도구.

사용 예:
  uv run --with pyserial tools/serial_console.py -t 30 --reset -c "12:info" -c "15:on" -c "20:off"
  -c "<초>:<명령>" 는 시작 후 해당 초에 명령 전송. 로그는 logs/serial_<시각>.log 에도 저장.
"""

import argparse
import datetime as dt
import pathlib
import sys
import time

import serial

ap = argparse.ArgumentParser()
ap.add_argument("-p", "--port", default="/dev/cu.usbmodem101")
ap.add_argument("-t", "--time", type=float, default=15.0, help="수신 시간(초)")
ap.add_argument("-c", "--cmd", action="append", default=[], help="<초>:<명령>")
ap.add_argument("--reset", action="store_true", help="열기 직후 RTS 펄스로 칩 리셋")
ap.add_argument("--until", default=None, help="이 문자열이 보이면 종료")
args = ap.parse_args()

cmds = []
for c in args.cmd:
    at, _, text = c.partition(":")
    cmds.append((float(at), text))
cmds.sort()

log_dir = pathlib.Path(__file__).resolve().parent.parent / "logs"
log_dir.mkdir(exist_ok=True)
log_path = log_dir / f"serial_{dt.datetime.now():%Y%m%d-%H%M%S}.log"
log_file = log_path.open("w", encoding="utf-8")


def emit(line: str) -> None:
    stamp = dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    out = f"{stamp} | {line}"
    print(out, flush=True)
    log_file.write(out + "\n")
    log_file.flush()


def open_port() -> serial.Serial:
    s = serial.Serial()
    s.port = args.port
    s.baudrate = 115200
    s.timeout = 0.1
    # USB-Serial-JTAG: RTS=1, DTR=0 조합이 칩 리셋. 열기 직후 OS가 둘 다 올린 상태에서
    # pyserial은 DTR부터 갱신하므로 DTR=1 유지 + RTS=0 으로 리셋 조합 회피
    s.dtr = True
    s.rts = False
    s.open()
    return s


start = time.time()
ser = None
while ser is None and time.time() - start < 10:
    try:
        ser = open_port()
    except serial.SerialException:
        time.sleep(0.3)
if ser is None:
    print(f"포트 열기 실패: {args.port}", file=sys.stderr)
    sys.exit(1)

if args.reset:
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    emit("--- reset ---")

start = time.time()
buf = b""
while time.time() - start < args.time:
    elapsed = time.time() - start
    while cmds and cmds[0][0] <= elapsed:
        _, text = cmds.pop(0)
        emit(f">>> {text}")
        try:
            ser.write((text + "\n").encode())
        except serial.SerialException:
            emit("!!! 전송 실패")
    try:
        chunk = ser.read(512)
    except (serial.SerialException, OSError):
        # 리셋 후 USB 재열거 대기
        try:
            ser.close()
        except Exception:
            pass
        time.sleep(0.3)
        try:
            ser = open_port()
            emit("--- reopened ---")
        except serial.SerialException:
            pass
        continue
    if not chunk:
        continue
    buf += chunk
    while b"\n" in buf:
        raw, buf = buf.split(b"\n", 1)
        line = raw.decode("utf-8", "replace").rstrip("\r")
        emit(line)
        if args.until and args.until in line:
            log_file.close()
            sys.exit(0)

log_file.close()
print(f"[log] {log_path}")
