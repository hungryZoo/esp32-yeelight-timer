# PlatformIO pre 스크립트: 빌드 전 include/secrets.h 점검
#   - 파일 없음 또는 예시 값 그대로: 터미널이면 값을 입력받아 작성 후 빌드 계속, 아니면 안내 출력 후 빌드 중단
#   - 값 규칙: WIFI_SSID 1-32바이트, WIFI_PASSWORD 빈 값(개방형) 또는 8-64자, OTA_PASSWORD 필수
import getpass
import os
import pathlib
import re
import secrets as pysecrets
import sys

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402  (PlatformIO SCons 전역)

Import("env")  # noqa: F821

# 빌드가 아닌 작업 (IDE 인덱싱, 플래시 삭제 등)은 점검 생략
SKIP_TARGETS = {"idedata", "__idedata", "erase", "envdump", "compiledb", "menuconfig"}

PROJECT = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
SECRETS = PROJECT / "include" / "secrets.h"
EXAMPLE = PROJECT / "include" / "secrets.h.example"
PLACEHOLDER = {"WIFI_SSID": "your-ssid", "WIFI_PASSWORD": "your-password", "OTA_PASSWORD": "change-me"}
DEFINE_RE = r'^(\s*#define\s+{name}\s+)"((?:[^"\\]|\\.)*)"'


def read_values(text):
    vals = {}
    for name in PLACEHOLDER:
        m = re.search(DEFINE_RE.format(name=name), text, re.M)
        if m:
            vals[name] = re.sub(r"\\(.)", r"\1", m.group(2))
    return vals


def check_ssid(v):
    if v in ("", PLACEHOLDER["WIFI_SSID"]):
        return "WiFi 이름 입력 필요"
    if len(v.encode("utf-8")) > 32:
        return "WiFi 이름은 32바이트 이하"
    return None


def check_wifi_pw(v):
    if v == PLACEHOLDER["WIFI_PASSWORD"]:
        return "예시 값(your-password) 그대로"
    if v and not 8 <= len(v) <= 64:
        return "WiFi 비밀번호는 8-64자 (개방형 네트워크는 빈 값)"
    return None


def check_ota_pw(v):
    if v in ("", PLACEHOLDER["OTA_PASSWORD"]):
        return "OTA 비밀번호 입력 필요 (예시 값 change-me 사용 불가)"
    return None


CHECKS = {"WIFI_SSID": check_ssid, "WIFI_PASSWORD": check_wifi_pw, "OTA_PASSWORD": check_ota_pw}


def find_problems(vals):
    probs = {}
    for name, check in CHECKS.items():
        if name not in vals:
            probs[name] = "#define 없음"
        elif err := check(vals[name]):
            probs[name] = err
    return probs


def open_console():
    # SCons 출력은 PlatformIO가 파이프로 받으므로 질문과 입력은 터미널 장치로 직접 처리
    # 텍스트 모드 r+ 는 비탐색(non-seekable) 장치에서 실패하므로 읽기, 쓰기 핸들 분리
    name_in, name_out = ("CONIN$", "CONOUT$") if os.name == "nt" else ("/dev/tty", "/dev/tty")
    try:
        return open(name_in, encoding="utf-8"), open(name_out, "w", encoding="utf-8")
    except OSError:
        return None


def ask(cin, cout, prompt, check, secret=False, default=None):
    while True:
        if secret:
            value = getpass.getpass(prompt)
        else:
            cout.write(prompt)
            cout.flush()
            line = cin.readline()
            if not line:
                raise EOFError
            value = line.rstrip("\r\n")
        if value == "" and default is not None:
            value = default
        err = check(value)
        if not err:
            return value
        cout.write(f"  -> {err}\n")
        cout.flush()


def ask_wifi_pw(cin, cout):
    while True:
        pw = ask(cin, cout, "WiFi 비밀번호 (입력 숨김, 개방형 네트워크는 Enter): ", check_wifi_pw, secret=True)
        if pw == "":
            return pw
        if getpass.getpass("WiFi 비밀번호 확인: ") == pw:
            return pw
        cout.write("  -> 두 입력이 다름. 다시 입력\n")
        cout.flush()


def c_string(v):
    return v.replace("\\", "\\\\").replace('"', '\\"')


def write_values(new_vals):
    if SECRETS.exists():
        text = SECRETS.read_text(encoding="utf-8")
    else:
        text = EXAMPLE.read_text(encoding="utf-8").replace(
            "// secrets.h 템플릿. include/secrets.h 로 복사 후 값 입력.",
            "// tools/check_secrets.py 가 첫 빌드 시 생성. 값 변경은 이 파일 직접 수정.",
            1,
        )
    for name, value in new_vals.items():
        pat = re.compile(DEFINE_RE.format(name=name), re.M)
        if pat.search(text):
            text = pat.sub(lambda m: m.group(1) + '"' + c_string(value) + '"', text, count=1)
        else:
            text += f'\n#define {name} "{c_string(value)}"\n'
    SECRETS.write_text(text, encoding="utf-8")


def guide(probs):
    lines = [
        "",
        "=" * 66,
        " WiFi 설정 필요: include/secrets.h",
        "-" * 66,
    ]
    if SECRETS.exists():
        lines += [f"   {name}: {msg}" for name, msg in probs.items()]
        lines += ["", " include/secrets.h 의 위 항목을 실제 값으로 수정 후 다시 빌드"]
    else:
        lines += [
            " (1) cp include/secrets.h.example include/secrets.h",
            " (2) include/secrets.h 에 WIFI_SSID, WIFI_PASSWORD, OTA_PASSWORD 입력",
            " (3) pio run 다시 실행",
        ]
    lines += [
        "",
        " 터미널에서 pio run 실행 시에는 값을 물어보고 파일을 자동 작성",
        " (secrets.h 는 .gitignore 대상, 저장소에 올라가지 않음)",
        "=" * 66,
        "",
    ]
    sys.stderr.write("\n".join(lines))
    sys.stderr.flush()


def interactive(console, probs):
    cin, cout = console
    cout.write(
        "\n" + "=" * 66 + "\n"
        " WiFi 설정 (최초 1회): include/secrets.h 작성\n"
        " 입력값은 이 컴퓨터의 include/secrets.h 에만 저장 (.gitignore 대상)\n"
        + "=" * 66 + "\n"
    )
    cout.flush()
    new_vals = {}
    if "WIFI_SSID" in probs:
        new_vals["WIFI_SSID"] = ask(cin, cout, "WiFi 이름 (2.4GHz SSID): ", check_ssid)
    if "WIFI_PASSWORD" in probs:
        new_vals["WIFI_PASSWORD"] = ask_wifi_pw(cin, cout)
    if "OTA_PASSWORD" in probs:
        auto = pysecrets.token_urlsafe(9)
        new_vals["OTA_PASSWORD"] = ask(
            cin, cout, f"OTA 업로드 비밀번호 (Enter = 자동 생성 {auto}): ", check_ota_pw, default=auto
        )
    write_values(new_vals)
    cout.write(f"\n -> {SECRETS.relative_to(PROJECT)} 저장 완료, 빌드 계속\n\n")
    cout.flush()


def main():
    if env.GetOption("clean") or SKIP_TARGETS & set(COMMAND_LINE_TARGETS):  # noqa: F821
        return
    vals = read_values(SECRETS.read_text(encoding="utf-8")) if SECRETS.exists() else {}
    probs = find_problems(vals)
    if not probs:
        return
    console = open_console()
    if console:
        try:
            interactive(console, probs)
            return
        except EOFError:
            pass
    guide(probs)
    env.Exit(1)  # noqa: F821


main()
