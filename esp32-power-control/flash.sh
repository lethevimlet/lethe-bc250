#!/usr/bin/env bash
# Build and flash the BC-250 ESP32 power controller firmware, from a laptop or desktop (Linux or
# macOS). Not from the console: OTA only arms while the console is off, and USB flashing with the
# +5VSB wire connected back-feeds the PSU (https://lethevimlet.github.io/lethe-bc250/power-firmware.html).
#
#   ./flash.sh            interactive: build, then pick USB upload / OTA update / nothing
#   ./flash.sh build      build only
#   ./flash.sh usb [PORT] [--erase]  build and upload over USB (first flash); --erase wipes the
#                        ESP32 first: settings made from the page (Wi-Fi, IP, OTA password, login) are gone
#   ./flash.sh ota [HOST] build and update over the air (console must be OFF, "OTA ready")
#
# Installs arduino-cli + the ESP32 core under your home (no root). Your Wi-Fi/OTA values are asked
# once and kept in config.local next to this script (gitignored, never in the sketch).
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SKETCH=$HERE/bc250_power_opto.ino
CONF=$HERE/config.local
FQBN=esp32:esp32:esp32c3:CDCOnBoot=cdc
WORK=${BC250_ESP32_WORK:-$HOME/.local/share/bc250-esp32}
ACLI=$WORK/arduino-cli
ACLI_CFG=$WORK/arduino-cli.yaml
OUT=$WORK/out
ESP32_INDEX=https://espressif.github.io/arduino-esp32/package_esp32_index.json

c_bold=$'\e[1m'; c_ok=$'\e[32m'; c_warn=$'\e[33m'; c_err=$'\e[31m'; c_off=$'\e[0m'
say()  { printf '%s==> %s%s\n' "$c_bold" "$*" "$c_off"; }
ok()   { printf '%s    %s%s\n' "$c_ok" "$*" "$c_off"; }
warn() { printf '%s    %s%s\n' "$c_warn" "$*" "$c_off"; }
die()  { printf '%serror: %s%s\n' "$c_err" "$*" "$c_off" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

[ -f "$SKETCH" ] || die "sketch not found next to this script: $SKETCH"
grep -qi "BC-250" /sys/class/dmi/id/product_name 2>/dev/null && die "this is the BC-250 itself: the ESP32 that controls its power cannot be flashed from here (OTA needs the console OFF). Run this on another PC."
have curl || die "curl is required"; have python3 || die "python3 is required (for the OTA uploader)"
if [ ! -t 0 ] && { exec 3</dev/tty; } 2>/dev/null; then exec <&3; fi   # keyboard from the terminal when piped

# ------------------------------------------------------------------ toolchain (arduino-cli + esp32 core)
acli() { "$ACLI" --config-file "$ACLI_CFG" "$@"; }
ensure_cli() {
    mkdir -p "$WORK"
    if [ ! -x "$ACLI" ]; then
        local os arch asset url
        case "$(uname -s)" in Linux) os=Linux ;; Darwin) os=macOS ;; *) die "unsupported OS $(uname -s)";; esac
        case "$(uname -m)" in x86_64|amd64) arch=64bit ;; aarch64|arm64) arch=ARM64 ;; armv7l) arch=ARMv7 ;; *) die "unsupported CPU $(uname -m)";; esac
        asset="${os}_${arch}.tar.gz"
        say "Downloading arduino-cli ($asset)"
        url=$(curl -fsSL https://api.github.com/repos/arduino/arduino-cli/releases/latest | grep browser_download_url | grep "$asset" | head -1 | cut -d'"' -f4)
        [ -n "$url" ] || die "could not find an arduino-cli release for $asset"
        curl -fsSL "$url" -o "$WORK/acli.tgz" && tar -xzf "$WORK/acli.tgz" -C "$WORK" arduino-cli && rm -f "$WORK/acli.tgz"
    fi
    if [ ! -f "$ACLI_CFG" ]; then
        "$ACLI" config init --overwrite --dest-file "$ACLI_CFG" >/dev/null
        acli config set board_manager.additional_urls "$ESP32_INDEX"
    fi
    if ! acli core list 2>/dev/null | grep -q '^esp32:esp32'; then
        say "Installing the ESP32 board core (a few hundred MB, one time)"
        acli core update-index >/dev/null
        acli core install esp32:esp32
    fi
    ok "arduino-cli $(acli version | grep -oE 'Version: [^ ]+' | cut -d' ' -f2), esp32 core $(acli core list | awk '/^esp32:esp32/{print $2}')"
}

# ------------------------------------------------------------------ your values (config.local)
ask() {   # VAR "prompt" [secret]
    local var=$1 prompt=$2 secret=${3:-} v
    while :; do
        if [ -n "$secret" ]; then read -r -s -p "$prompt: " v; echo; else read -r -p "$prompt: " v; fi
        [ -n "$v" ] && break; echo "  (required)"
    done
    printf -v "$var" '%s' "$v"
}
load_conf() {
    WIFI_SSID=""; WIFI_PASS=""; OTA_PASS=""; CONSOLE_API=""; MDNS_NAME=bc250; ESP32_HOST=""; WEB_LOGIN=""
    # shellcheck disable=SC1090
    [ -f "$CONF" ] && . "$CONF"
    [ -n "$WIFI_SSID" ]   || ask WIFI_SSID "Wi-Fi network name (SSID)"
    [ -n "$WIFI_PASS" ]   || { read -r -s -p "Wi-Fi password (8-63 chars; empty = open network): " WIFI_PASS; echo; }
    [ -n "$OTA_PASS" ]    || ask OTA_PASS "OTA password (needed for every later update; keep it)" secret
    [ -n "$CONSOLE_API" ] || { ask CONSOLE_API "bc250-api address on the console, e.g. http://192.168.1.50:8250 (changeable later from the page)"; }
    case "$CONSOLE_API" in http://*|https://*) ;; *) CONSOLE_API="http://$CONSOLE_API" ;; esac
    case "$CONSOLE_API" in *:*:*) ;; *) CONSOLE_API="$CONSOLE_API:8250" ;; esac
    { echo "# lethe-bc250 ESP32 build values (gitignored). Delete a line to be asked again."
      printf 'WIFI_SSID=%q\nWIFI_PASS=%q\nOTA_PASS=%q\nCONSOLE_API=%q\nMDNS_NAME=%q\nESP32_HOST=%q\nWEB_LOGIN=%q\n' "$WIFI_SSID" "$WIFI_PASS" "$OTA_PASS" "$CONSOLE_API" "$MDNS_NAME" "$ESP32_HOST" "$WEB_LOGIN"
    } > "$CONF"; chmod 600 "$CONF"
}
save_host() { python3 - "$CONF" "$1" <<'PY'
import re, sys; p, h = sys.argv[1], sys.argv[2]; s = open(p).read()
s = re.sub(r'^ESP32_HOST=.*$', 'ESP32_HOST=' + h, s, flags=re.M); open(p, 'w').write(s)
PY
}

# ------------------------------------------------------------------ build
build() {
    local d="$WORK/build/bc250_power_opto"
    mkdir -p "$d" "$OUT"
    # placeholders -> your values, in a copy; the repo sketch stays generic
    python3 - "$SKETCH" "$d/bc250_power_opto.ino" "$WIFI_SSID" "$WIFI_PASS" "$OTA_PASS" "$CONSOLE_API" "$MDNS_NAME" <<'PY'
import sys
src, dst, ssid, pw, ota, api, mdns = sys.argv[1:]
s = open(src).read()
def cstr(v): return v.replace('\\', '\\\\').replace('"', '\\"')
for old, new in (('"YOUR_SSID"', cstr(ssid)), ('"YOUR_PASSWORD"', cstr(pw)), ('"CHANGE_ME"', cstr(ota)),
                 ('"http://YOUR_CONSOLE_IP:8250"', cstr(api)), ('*MDNS_NAME = "bc250"', '*MDNS_NAME = "%s"' % cstr(mdns))):
    if old not in s: sys.exit("placeholder %s not found in the sketch" % old)
    s = s.replace(old, new if old.startswith('*') else '"%s"' % new, 1)
open(dst, 'w').write(s)
PY
    say "Compiling for ESP32-C3 (first build downloads the compiler, later ones take ~1 min)"
    # a stale binary from an earlier build must never pass for this one
    rm -f "$OUT/bc250_power_opto.ino.bin"
    if ! acli compile --fqbn "$FQBN" --output-dir "$OUT" "$d" > "$WORK/compile.log" 2>&1; then
        grep -E "error|Error" "$WORK/compile.log" | head -20
        die "build failed (full log: $WORK/compile.log)"
    fi
    grep -E "Sketch uses|Global variables" "$WORK/compile.log" || true
    [ -f "$OUT/bc250_power_opto.ino.bin" ] || die "build produced no binary (log: $WORK/compile.log)"
    ok "firmware: $OUT/bc250_power_opto.ino.bin"
}

# ------------------------------------------------------------------ USB
ports() {
    ls /dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null || true
}
usb() {
    local port=${1:-} erase=0
    [ "${2:-}" = --erase ] && erase=1
    [ "$port" = --erase ] && { erase=1; port=""; }
    warn "USB flashing: the ESP32 must NOT be wired to the PSU's +5VSB while on USB (docs: Firmware, flashing and OTA). Power it from the USB cable only."
    if [ -z "$port" ]; then
        local list; list=$(ports)
        [ -n "$list" ] || die "no serial port found. Plug the ESP32-C3 in over USB (hold BOOT while plugging if it does not show up)"
        if [ "$(echo "$list" | wc -l)" -eq 1 ]; then port=$list
        else echo "$list" | nl; read -r -p "port number: " n; port=$(echo "$list" | sed -n "${n}p"); fi
    fi
    local fqbn=$FQBN
    if [ $erase = 1 ]; then fqbn="$FQBN,EraseFlash=all"; say "Uploading over $port, erasing the whole flash first (page settings and OTA password go back to the compiled defaults)"
    else say "Uploading over $port"; fi
    if ! acli upload --fqbn "$fqbn" -p "$port" --input-dir "$OUT"; then
        [ "$(uname -s)" = Linux ] && warn "permission denied? add yourself to the serial group: sudo usermod -aG dialout \$USER (or uucp on Arch), then log out and in"
        die "upload failed"
    fi
    ok "flashed. Watch the serial monitor for the MAC to reserve on the router: $ACLI monitor -p $port -c baudrate=115200"
}

# ------------------------------------------------------------------ OTA
ota() {
    local host=${1:-$ESP32_HOST} st espota
    [ -n "$host" ] || ask host "ESP32 address (IP, or bc250.local where mDNS works)"
    host=${host#http://}; host=${host%%/*}
    say "Checking $host"
    # the page may have its login on: WEB_LOGIN=user:pass in config.local gets past it
    local auth=(); [ -n "$WEB_LOGIN" ] && auth=(--digest -u "$WEB_LOGIN")
    local code; code=$(curl -s -o /dev/null -w '%{http_code}' -m 5 "${auth[@]}" "http://$host/rest/status" || true)
    [ "$code" = 401 ] && die "the page at $host asks for a login: put WEB_LOGIN=user:pass into $CONF (or the wrong one is there)"
    st=$(curl -fsS -m 5 "${auth[@]}" "http://$host/rest/status") || die "no answer from http://$host/rest/status"
    echo "    $st"
    echo "$st" | grep -q '"state":"OFF"' || die "the console is not OFF (state $(echo "$st" | grep -oE '"state":"[A-Z]*"' | cut -d'"' -f4)). Shut it down first: OTA reboots the ESP32, which would hard-cut a running console."
    echo "$st" | grep -q '"ota":true'   || die "OTA is not armed yet; wait a few seconds after the console reports OFF and retry"
    save_host "$host"
    espota=$(ls "$HOME"/.arduino15/packages/esp32/hardware/esp32/*/tools/espota.py 2>/dev/null | tail -1)
    [ -n "$espota" ] || espota=$(find "$WORK" "$HOME/Library/Arduino15" -name espota.py 2>/dev/null | tail -1)
    [ -n "$espota" ] || die "espota.py not found in the ESP32 core"
    say "Updating $host over the air"
    python3 "$espota" -i "$host" -p 3232 -a "$OTA_PASS" -f "$OUT/bc250_power_opto.ino.bin" -r 2>&1 | tr '\r' '\n' | grep -vE '^Uploading|^\s*$' | tail -3
    say "Waiting for the ESP32 to come back"
    sleep 8; local i; for i in $(seq 1 12); do st=$(curl -fsS -m 4 "${auth[@]}" "http://$host/rest/status" 2>/dev/null) && break; sleep 5; done
    [ -n "$st" ] || die "the ESP32 did not answer after the update (it keeps the previous firmware if the upload was rejected)"
    ok "back: uptime $(echo "$st" | grep -oE '"uptime":[0-9]+' | cut -d: -f2) s, console $(echo "$st" | grep -oE '"console":"[^"]*"' | cut -d'"' -f4)"
}

# ------------------------------------------------------------------ main
action=${1:-}; arg=${2:-}
ensure_cli
load_conf
build
case "$action" in
    build) ;;
    usb) usb "$arg" "${3:-}" ;;
    ota) ota "$arg" ;;
    "")
        echo; echo "  1) upload over USB (first flash)"; echo "  2) update over the air (console must be OFF)"; echo "  3) nothing, the binary is built"
        read -r -p "choice [3]: " c
        case "${c:-3}" in 1) usb ;; 2) ota ;; *) ;; esac ;;
    *) die "usage: $0 [build|usb [PORT]|ota [HOST]]" ;;
esac
