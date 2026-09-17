# PlatformIO extra script: embeds ../docs/openapi.json as include/openapi_json.h so the
# device can serve it at /openapi.json. Run automatically before each build.

import json
import os
import sys

Import("env")  # noqa: F821

project = env.subst("$PROJECT_DIR")
spec_path = os.path.join(project, "..", "docs", "openapi.json")
header = os.path.join(project, "include", "openapi_json.h")

if not os.path.isfile(spec_path):
    sys.stderr.write("ERROR: %s missing\n" % spec_path)
    env.Exit(1)

with open(spec_path, encoding="utf-8") as f:
    try:
        spec = json.load(f)
    except json.JSONDecodeError as e:
        sys.stderr.write("ERROR: docs/openapi.json is not valid JSON: %s\n" % e)
        env.Exit(1)

# minified, and with the firmware version filled in so the document never drifts from the build
version = env.subst("$PIOENV")
for flag in env.get("BUILD_FLAGS", []):
    if "FW_VERSION" in str(flag):
        version = str(flag).split("=", 1)[1].strip('\\"')
spec.setdefault("info", {})["version"] = version
body = json.dumps(spec, separators=(",", ":"), ensure_ascii=True)

if ")raw" in body:
    sys.stderr.write("ERROR: spec contains a raw string terminator\n")
    env.Exit(1)

os.makedirs(os.path.dirname(header), exist_ok=True)
content = ('#pragma once\n\n#include <Arduino.h>\n\n'
           '// Generated from docs/openapi.json by scripts/embed_openapi.py — do not edit.\n'
           'const char OPENAPI_JSON[] PROGMEM = R"rawjson(%s)rawjson";\n' % body)

old = ""
if os.path.isfile(header):
    with open(header, encoding="utf-8") as f:
        old = f.read()
if old != content:
    with open(header, "w", encoding="utf-8") as f:
        f.write(content)
    print("embedded openapi.json (%d bytes) as include/openapi_json.h" % len(body))
