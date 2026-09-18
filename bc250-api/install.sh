#!/usr/bin/env bash
# Install (or update) bc250-api on the BC-250: run from this folder with sudo.
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo "run with sudo" >&2; exit 1; }
cd "$(dirname "$0")"
install -m 0755 -o root -g root bc250-api /usr/local/bin/bc250-api
install -m 0644 -o root -g root bc250-api.service /etc/systemd/system/bc250-api.service
install -d -m 0755 /usr/local/share/bc250-api
install -m 0644 -o root -g root panel.js /usr/local/share/bc250-api/panel.js
systemctl daemon-reload
systemctl enable --now bc250-api.service
systemctl restart bc250-api.service
sleep 1
systemctl --no-pager --lines=3 status bc250-api.service || true
echo "bc250-api: http://$(hostname -I | awk '{print $1}'):8250/api/status"
