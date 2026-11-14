#!/usr/bin/env bash
# Build the reconstructed ESP32-BlueJammer sketch from this release.
#
#   bash tools/build.sh <path/to/Official-Combo-Channel_BT-BLE-WiFi-RC.ino.bin>
#
# The original BIN is not distributed. The script generates the recovered
# data headers (bitmaps.h, banners.h) into source/ESP32-BlueJammer, then
# compiles with arduino-cli for esp32:esp32:esp32.
#
# Requirements:
#   - arduino-cli 1.6.x
#   - esp32:esp32 core 2.0.10
#   - RF24 1.6.2, Adafruit SSD1306 2.5.17, Adafruit GFX Library 1.13.0,
#     Adafruit BusIO 1.17.4 (installed automatically if missing)
#   - Python 3 (standard library only)
#
# Environment overrides: ARDUINO_CLI, BUILD_DIR, FQBN.
set -euo pipefail

usage() {
  echo "usage: bash tools/build.sh <path/to/Official-...ino.bin>" >&2
  exit 2
}

[ $# -ge 1 ] || usage
RELEASE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_DIR="$RELEASE_ROOT/source/ESP32-BlueJammer"
BUILD_DIR="${BUILD_DIR:-$RELEASE_ROOT/build}"
FQBN="${FQBN:-esp32:esp32:esp32}"
ORIGINAL_BIN="$1"
case "$ORIGINAL_BIN" in
  /*) ;;
  *) ORIGINAL_BIN="$(pwd)/$ORIGINAL_BIN" ;;
esac

[ -f "$ORIGINAL_BIN" ] || { echo "ERROR: BIN not found: $ORIGINAL_BIN" >&2; exit 2; }
[ -f "$SOURCE_DIR/ESP32-BlueJammer.ino" ] || { echo "ERROR: sketch missing: $SOURCE_DIR" >&2; exit 2; }

ARDUINO_CLI="${ARDUINO_CLI:-$(command -v arduino-cli || true)}"
[ -n "$ARDUINO_CLI" ] || { echo "ERROR: arduino-cli not found" >&2; exit 2; }
command -v python3 >/dev/null || { echo "ERROR: python3 not found" >&2; exit 2; }

# ---- toolchain checks ------------------------------------------------------
CORE_JSON="$("$ARDUINO_CLI" core list --format json)"
CORE_VERSION="$(printf '%s' "$CORE_JSON" | python3 -c '
import json, sys
for p in json.load(sys.stdin).get("platforms", []):
    if p.get("id") == "esp32:esp32":
        print(p.get("installed_version", "")); break
')"
[ "$CORE_VERSION" = "2.0.10" ] || {
  echo "ERROR: esp32:esp32 $CORE_VERSION installed; this build is verified with 2.0.10" >&2
  echo "       run: arduino-cli core install esp32:esp32@2.0.10" >&2
  exit 2
}

# ---- pinned libraries ------------------------------------------------------
install_lib() {
  local spec="$1" name="${1%@*}" want="${1##*@}" have
  have="$("$ARDUINO_CLI" lib list --format json | python3 -c '
import json, sys
name = sys.argv[1]
for lib in json.load(sys.stdin).get("installed_libraries", []):
    info = lib.get("library", {})
    if info.get("name") == name:
        print(info.get("version", "")); break
' "$name")"
  if [ "$have" != "$want" ]; then
    echo "== installing $spec (have: ${have:-none}) =="
    "$ARDUINO_CLI" lib install "$spec"
  fi
}
install_lib "RF24@1.6.2"
install_lib "Adafruit SSD1306@2.5.17"
install_lib "Adafruit GFX Library@1.13.0"
install_lib "Adafruit BusIO@1.17.4"

# ---- generate recovered headers -------------------------------------------
echo "== generating headers from the supplied BIN =="
python3 "$RELEASE_ROOT/tools/gen_assets.py" "$ORIGINAL_BIN" --out "$SOURCE_DIR"

# ---- compile ---------------------------------------------------------------
echo "== compiling =="
mkdir -p "$BUILD_DIR"
"$ARDUINO_CLI" compile --fqbn "$FQBN" --build-path "$BUILD_DIR" "$SOURCE_DIR"

IMAGE="$(ls -t "$BUILD_DIR"/*.ino.bin 2>/dev/null | head -1)"
[ -n "$IMAGE" ] || { echo "ERROR: no .ino.bin produced" >&2; exit 1; }

# ---- verify expected image facts ------------------------------------------
read -r SIZE ENTRY < <(python3 - "$IMAGE" <<'PY'
import os, struct, sys
path = sys.argv[1]
data = open(path, "rb").read()
if not data or data[0] != 0xE9:
    raise SystemExit("not an ESP32 application image")
entry = struct.unpack_from("<I", data, 4)[0]
print(os.path.getsize(path), "0x%08x" % entry)
PY
)
echo "  image      : $IMAGE"
echo "  size       : $SIZE bytes"
echo "  entry      : $ENTRY"
echo "  segments   : $(python3 -c 'import sys; d=open(sys.argv[1],"rb").read(); print(d[1])' "$IMAGE")"

[ "$SIZE" = "885136" ] || { echo "ERROR: expected 885136 bytes, got $SIZE" >&2; exit 1; }
[ "$ENTRY" = "0x40082f0c" ] || { echo "ERROR: expected entry 0x40082f0c, got $ENTRY" >&2; exit 1; }
echo "== OK: image matches the documented default build =="
