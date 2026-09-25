#!/usr/bin/env bash
# lethe-bc250 guided installer.
#
#   curl -fsSL https://raw.githubusercontent.com/lethevimlet/lethe-bc250/main/install.sh | bash
#   # or, to read it first:
#   git clone https://github.com/lethevimlet/lethe-bc250 && cd lethe-bc250 && ./install.sh
#
# On a BC-250 it clones (or updates) the repo into ~/lethe-bc250 and lets you pick what to install:
# bc250-tune with its boot service, Decky Loader, the Tune and Sleep plugins, bc250-api, Sunshine
# (game streaming to Moonlight). On any
# other machine it offers the ESP32 firmware build/flash helper (the ESP32 cannot be flashed from
# the console: OTA only arms while the console is off). Re-run it any time: every step is idempotent
# and re-running is the update path.
#
# Options (non-interactive):  --all | --only tune,decky,tune-plugin,sleep-plugin,api,sunshine,esp32
#                             --dir PATH (checkout location, default ~/lethe-bc250)  --no-update
set -euo pipefail

usage() { cat <<'EOT'
lethe-bc250 guided installer
  curl -fsSL https://raw.githubusercontent.com/lethevimlet/lethe-bc250/main/install.sh | bash
  ./install.sh [--all | --only tune,decky,tune-plugin,sleep-plugin,api,sunshine,esp32] [--dir PATH] [--no-update]
On a BC-250: bc250-tune + boot services, Decky Loader, the Tune and Sleep plugins, bc250-api, Sunshine.
On any other machine: the ESP32 firmware build/flash helper.
EOT
}


main() {
REPO_URL=https://github.com/lethevimlet/lethe-bc250.git
DIR=${LETHE_BC250_DIR:-$HOME/lethe-bc250}
SELECT=""; MODE=menu; UPDATE=1

while [ $# -gt 0 ]; do
    case "$1" in
        --all) MODE=all ;;
        --only) MODE=only; SELECT=${2:-}; shift ;;
        --dir) DIR=${2:?}; shift ;;
        --no-update) UPDATE=0 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

# When piped into bash, stdin is the script: take the keyboard from the terminal instead.
# Piped into bash (curl | bash), stdin is the script itself. bash reads a piped script command by
# command, so swapping stdin at top level would make it wait for the REST OF THE SCRIPT on the
# keyboard and print nothing. Everything therefore runs inside main(), which bash has parsed in
# full before the first command executes; only then is it safe to take the keyboard from the tty.
if [ ! -t 0 ] && { exec 3</dev/tty; } 2>/dev/null; then exec <&3; fi

c_bold=$'\e[1m'; c_dim=$'\e[2m'; c_ok=$'\e[32m'; c_warn=$'\e[33m'; c_err=$'\e[31m'; c_off=$'\e[0m'
say()  { printf '%s==> %s%s\n' "$c_bold" "$*" "$c_off"; }
ok()   { printf '%s    ok: %s%s\n' "$c_ok" "$*" "$c_off"; }
warn() { printf '%s    %s%s\n' "$c_warn" "$*" "$c_off"; }
die()  { printf '%serror: %s%s\n' "$c_err" "$*" "$c_off" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

is_bc250() { grep -qi "BC-250" /sys/class/dmi/id/product_name 2>/dev/null; }

# ------------------------------------------------------------------ the checkout
# Running from inside a checkout (./install.sh) uses that checkout; otherwise clone/update $DIR.
here=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd || true)
if [ -n "$here" ] && [ -x "$here/decky-bc250-tune/bc250-tune" ]; then
    DIR=$here
else
    have git || die "git is required (on Bazzite it is preinstalled; elsewhere install it first)"
    if [ -d "$DIR/.git" ]; then
        if [ $UPDATE = 1 ]; then say "Updating $DIR"; git -C "$DIR" pull --ff-only || warn "pull failed, using the checkout as is"; fi
    else
        say "Cloning into $DIR"; git clone --depth 1 "$REPO_URL" "$DIR"
    fi
fi
cd "$DIR"

# ------------------------------------------------------------------ what can be installed here
declare -A DESC=(
    [tune]="bc250-tune: VRAM split, GPU range, CU / core unlocks, fan curve, HUD, resolution + boot services"
    [decky]="Decky Loader (needed by the two plugins; installed with ujust if missing)"
    [tune-plugin]="BC-250 Tune plugin for the Steam Quick Access menu"
    [sleep-plugin]="BC-250 Sleep plugin: fake sleep, wake on any button, quiet fans"
    [api]="bc250-api: stats and switches over the LAN (feeds the ESP32 page)"
    [sunshine]="Sunshine: stream Gaming Mode to Moonlight on a phone, laptop or TV box (Bazzite's brew build, KMS capture)"
    [esp32]="ESP32 power controller firmware: build, flash over USB, or update over the air"
)
if is_bc250; then ITEMS=(tune decky tune-plugin sleep-plugin api sunshine); else ITEMS=(esp32); fi

case $MODE in
    all) CHOSEN=("${ITEMS[@]}") ;;
    only)
        IFS=, read -r -a CHOSEN <<<"$SELECT"
        for c in "${CHOSEN[@]}"; do [[ " ${ITEMS[*]} " == *" $c "* ]] || die "'$c' is not available on this machine (choices: ${ITEMS[*]})"; done ;;
    menu)
        if is_bc250; then intro="This is a BC-250. Pick what to install (Space toggles, Enter confirms)."
        else intro="This is not a BC-250, so only the ESP32 firmware helper applies here. Run this installer on the console for the rest."; fi
        if have whiptail; then
            args=(); for i in "${ITEMS[@]}"; do args+=("$i" "${DESC[$i]}" ON); done
            sel=$(whiptail --title "lethe-bc250 installer" --checklist "$intro" 20 100 "${#ITEMS[@]}" "${args[@]}" 3>&1 1>&2 2>&3) || { echo "cancelled"; exit 0; }
            read -r -a CHOSEN <<<"$(echo "$sel" | tr -d '"')"
        else
            echo "$intro"; CHOSEN=()
            for i in "${ITEMS[@]}"; do
                read -r -p "Install ${DESC[$i]}? [Y/n] " a; [[ ${a:-y} =~ ^[Nn] ]] || CHOSEN+=("$i")
            done
        fi ;;
esac
[ ${#CHOSEN[@]} -gt 0 ] || { echo "nothing selected"; exit 0; }

need_root=0; for c in "${CHOSEN[@]}"; do [ "$c" != esp32 ] && need_root=1; done
if [ $need_root = 1 ]; then say "Root is needed for the console-side steps"; sudo -v || die "sudo failed"; fi

# ------------------------------------------------------------------ steps
PLUGINS=$HOME/homebrew/plugins
restart_loader=0; failed=(); todo=()

install_plugin() {   # name  source-dir
    local name=$1 src=$2
    sudo mkdir -p "$PLUGINS/$name"
    sudo cp -r "$src/plugin.json" "$src/package.json" "$src/main.py" "$src/LICENSE" "$src/dist" "$PLUGINS/$name/"
    sudo chown -R root:root "$PLUGINS/$name"
    restart_loader=1
}

step_tune() { sudo "$DIR/decky-bc250-tune/bc250-tune" install; }

step_decky() {
    if [ -x "$HOME/homebrew/services/PluginLoader" ]; then ok "Decky Loader already installed"
    elif have ujust; then ujust setup-decky install
    else warn "no ujust here: install Decky Loader from https://github.com/SteamDeckHomebrew/decky-loader and re-run"; return 1; fi
    # Bazzite ships the loader binary unreadable to others; the plugins need this
    sudo chmod a+rx "$HOME/homebrew/services" "$HOME/homebrew/services/PluginLoader" 2>/dev/null || true
}

step_tune_plugin()  { install_plugin bc250-tune  "$DIR/decky-bc250-tune/decky-plugin"; }
step_sleep_plugin() { install_plugin bc250-sleep "$DIR/decky-bc250-sleep"; }
step_api()          { sudo "$DIR/bc250-api/install.sh"; }
step_esp32()        { "$DIR/esp32-power-control/flash.sh"; }

# Sunshine the way Bazzite does it on deck images: the brew build, because Gaming Mode is gamescope
# on KMS and only KMS capture sees it (the flatpak cannot). ujust's recipe installs it, writes
# capture = kms and starts the user service, but its tail is not trusted: its unit override targets
# a unit name the current formula no longer uses (it exits 1 after a good install), its root step
# needs a tty, and its status check mistakes the brew unit for the flatpak. So the parts that matter
# are done here again, idempotently: capabilities (without them: "Failed to gain CAP_SYS_ADMIN",
# no encoder, no stream), the capture mode, the service, the firewall.
SUNSHINE_PORTS=(47984/tcp 47989/tcp 47990/tcp 48010/tcp 47998/udp 47999/udp 48000/udp 48002/udp 48010/udp)
BREW=/home/linuxbrew/.linuxbrew/bin/brew
sunshine_installed() { [ -d /home/linuxbrew/.linuxbrew/Cellar/sunshine ] || [ -e /home/linuxbrew/.linuxbrew/opt/sunshine ]; }
step_sunshine() {
    have ujust || { warn "no ujust here: this step is for Bazzite (see https://docs.bazzite.gg/Advanced/sunshine/)"; return 1; }
    [ -x $BREW ] || { warn "brew is missing (/home/linuxbrew); Bazzite ships it, run 'ujust setup-brew' or reinstall it first"; return 1; }
    if sunshine_installed; then
        ok "Sunshine (brew) already installed"
    else
        yes | ujust setup-sunshine enable-brew || true    # one Y/n question; the exit code is the recipe's tail
        sunshine_installed || { warn "brew did not install Sunshine (see above)"; return 1; }
    fi
    eval "$($BREW shellenv)"
    local bin; bin=$(readlink -f "$(command -v sunshine)") || { warn "sunshine is not on PATH"; return 1; }

    # KMS capture needs CAP_SYS_ADMIN on the binary; Bazzite's helper also loads uhid and reloads udev
    if [ -x /usr/libexec/sunshine-postinst ]; then sudo /usr/libexec/sunshine-postinst >/dev/null
    else sudo setcap cap_sys_admin,cap_sys_nice+p "$bin"; fi
    getcap "$bin" | grep -q cap_sys_admin || { warn "could not set the capabilities on $bin"; return 1; }

    local conf=$HOME/.config/sunshine/sunshine.conf
    mkdir -p "$(dirname "$conf")"
    grep -q '^capture *= *kms' "$conf" 2>/dev/null || { sed -i '/^capture *=/d' "$conf" 2>/dev/null; echo "capture = kms" >>"$conf"; }

    # the formula's unit (After=graphical-session, Restart=on-failure); restart so it runs with the caps
    brew services start sunshine >/dev/null 2>&1 || true
    local unit; unit=$(systemctl --user list-units --all --no-legend '*unshine*.service' | awk '{print $1}' | head -1)
    [ -n "$unit" ] || { warn "no sunshine user service after 'brew services start sunshine'"; return 1; }
    systemctl --user enable "$unit" >/dev/null 2>&1 || true
    systemctl --user restart "$unit"

    if have firewall-cmd && sudo firewall-cmd --state >/dev/null 2>&1; then
        local changed=0
        for p in "${SUNSHINE_PORTS[@]}"; do
            sudo firewall-cmd -q --permanent --query-port="$p" || { sudo firewall-cmd -q --permanent --add-port="$p"; changed=1; }
        done
        [ $changed = 0 ] || sudo firewall-cmd -q --reload
    fi

    sleep 8   # the unit sleeps 5 s before starting; then it probes the display and the encoder
    if journalctl --user -u "$unit" --since "-20s" --no-pager 2>/dev/null | grep -q "Failed to gain CAP_SYS_ADMIN\|Unable to find display"; then
        warn "Sunshine is up but found no display or encoder: is the TV connected and on? (journalctl --user -u $unit)"
    else
        ok "Sunshine is running ($unit); management page at https://<console-ip>:47990"
    fi
}

for c in "${CHOSEN[@]}"; do
    say "${DESC[$c]}"
    fn=step_${c//-/_}
    if "$fn"; then ok "$c done"; else warn "$c FAILED (see above)"; failed+=("$c"); fi
done

if [ $restart_loader = 1 ]; then
    say "Restarting Decky Loader so the plugins load"
    sudo systemctl restart plugin_loader || warn "could not restart plugin_loader; restart it (or reboot) yourself"
fi

# ------------------------------------------------------------------ what a human still has to do
echo
say "Done. Still to do by hand:"
if is_bc250; then
    todo+=("BIOS: IOMMU disabled, fan mode Default or Customize (not Full Speed), AUTO_PWRON1 jumper (https://lethevimlet.github.io/lethe-bc250/os.html)")
    todo+=("Gaming Mode: Performance Overlay level 1 for the HUD line; Quick Access → Decky for the plugins")
    todo+=("The ESP32 firmware is built and flashed from another PC: run this same installer there")
    todo+=("Config lives in /etc/bc250-tune/config; 'sudo bc250-tune menu' or the Decky plugin to change it")
    todo+=("Quieter idle with 4-pin PWM fans: 'sudo bc250-tune set fan-curve on' (off by default; BIOS fan mode not Full Speed)")
    [[ " ${CHOSEN[*]} " == *" sunshine "* ]] && todo+=("Sunshine: open https://<console-ip>:47990 from another device, set a user and password, then pair Moonlight (PIN). It streams what the TV shows, so the TV must be connected and on; no stream while the console is asleep. https://lethevimlet.github.io/lethe-bc250/streaming.html")
    sens=$(sudo /usr/local/bin/bc250-tune status --json 2>/dev/null | grep -o '"sensor":"[a-z-]*"' | cut -d'"' -f4 || true)
    case "$sens" in
        no-chip) todo+=("NOTE: this board's Super I/O does not answer, so there is no fan speed reading (HUD shows FAN n/a) and no fan curve / quiet fans. Usual cause: the ESP32 sense wire on TPMS1 sits on, or touches, an LPC pin instead of the 3.3 V pin. https://lethevimlet.github.io/lethe-bc250/power-page.html") ;;
        no-driver) todo+=("NOTE: the nct6687 fan driver is not on this image, so there is no fan speed reading and no fan curve. https://lethevimlet.github.io/lethe-bc250/lessons.html") ;;
    esac
else
    todo+=("On the console, run the installer again for bc250-tune, the plugins and bc250-api")
fi
for t in "${todo[@]}"; do echo "  • $t"; done
[ ${#failed[@]} -eq 0 ] || { echo; warn "Failed steps: ${failed[*]}. Fix the cause and re-run; finished steps are skipped or refreshed harmlessly."; exit 1; }
}

# one line on purpose: after main() returns, bash must not try to read more script from the tty
main "$@"; exit $?
