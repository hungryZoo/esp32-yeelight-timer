# PlatformIO extra script: include/secrets.h 의 OTA_PASSWORD 를 espota --auth 로 전달
import pathlib
import re

Import("env")  # noqa: F821  (PlatformIO SCons 전역)

secrets = pathlib.Path(env.subst("$PROJECT_DIR")) / "include" / "secrets.h"  # noqa: F821
m = re.search(r'#define\s+OTA_PASSWORD\s+"((?:[^"\\]|\\.)*)"', secrets.read_text(encoding="utf-8"))
if m:
    # post 스크립트 시점에는 플랫폼 main.py가 UPLOADERFLAGS를 이미 구성했으므로 직접 추가
    env.Append(UPLOADERFLAGS=["--auth=" + re.sub(r"\\(.)", r"\1", m.group(1))])  # noqa: F821
else:
    print("[ota_auth] include/secrets.h 에 OTA_PASSWORD 없음")
