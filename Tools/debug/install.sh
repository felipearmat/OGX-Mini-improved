#!/bin/sh
# Installs the probe reader (Tools/bench/probe.py) as a service of this user that starts at login
# and keeps running (also in SteamOS / Bazzite Game Mode): Linux systemd user unit, macOS launchd
# agent. It records the adapter's debug probe point to one file per session (Firmware/RP2040/docs/Debug_Mode.md).
#
#   Tools/debug/install.sh                        reader found by itself, files in ~/ogx-probe-logs
#   Tools/debug/install.sh --port /dev/ttyUSB0 --dir ~/logs/probe
#   Tools/debug/install.sh --uninstall
#
# Needs python3 (3.8+). pyserial goes in a venv of its own, in ~/.local/share/ogx-probe.
set -eu

NAME=ogx-probe
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PROBE="$REPO/Tools/bench/probe.py"
HOME_DIR=${XDG_DATA_HOME:-$HOME/.local/share}/ogx-probe
PORT=auto
LOGS="$HOME/ogx-probe-logs"
UNINSTALL=0

while [ $# -gt 0 ]; do
  case $1 in
    --port) PORT=$2; shift 2 ;;
    --dir) LOGS=$2; shift 2 ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
  esac
done

case $(uname -s) in
  Linux) OS=linux ;;
  Darwin) OS=mac ;;
  *) echo "Linux or macOS only; on Windows use Tools/debug/install.ps1" >&2; exit 1 ;;
esac

UNIT="$HOME/.config/systemd/user/$NAME.service"
PLIST="$HOME/Library/LaunchAgents/com.ogxmini.probe.plist"

if [ $UNINSTALL = 1 ]; then
  if [ $OS = linux ]; then
    systemctl --user disable --now "$NAME" 2>/dev/null || true
    rm -f "$UNIT"
    systemctl --user daemon-reload
  else
    launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
    rm -f "$PLIST"
  fi
  rm -rf "$HOME_DIR"
  echo "Removed. The session files in your log folder are kept."
  exit 0
fi

[ -f "$PROBE" ] || { echo "not found: $PROBE (run this from the repository)" >&2; exit 1; }
command -v python3 >/dev/null || { echo "python3 is needed" >&2; exit 1; }

echo "Python environment in $HOME_DIR"
python3 -m venv "$HOME_DIR/venv"
"$HOME_DIR/venv/bin/python" -m pip install --quiet --disable-pip-version-check --upgrade pyserial
PY="$HOME_DIR/venv/bin/python"
mkdir -p "$LOGS"
LOGS=$(cd "$LOGS" && pwd)

if [ $OS = linux ]; then
  mkdir -p "$(dirname "$UNIT")"
  cat > "$UNIT" <<EOF
[Unit]
Description=OGX-Mini debug probe reader (Tools/bench/probe.py), also in Game Mode

[Service]
ExecStart="$PY" -u "$PROBE" --port "$PORT" --dir "$LOGS" --quiet
Restart=always
RestartSec=5

[Install]
WantedBy=default.target
EOF
  systemctl --user daemon-reload
  systemctl --user enable --now "$NAME"
  # Serial ports belong to a group on some distributions (dialout, uucp).
  for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$dev" ] || continue
    if [ ! -r "$dev" ] || [ ! -w "$dev" ]; then
      echo "Note: $dev is not readable by you; add yourself to its group ($(stat -c %G "$dev")) and log in again."
    fi
  done
  echo "Installed: systemctl --user status $NAME   (journalctl --user -u $NAME for its messages)"
else
  mkdir -p "$(dirname "$PLIST")"
  cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.ogxmini.probe</string>
  <key>ProgramArguments</key>
  <array>
    <string>$PY</string><string>-u</string><string>$PROBE</string>
    <string>--port</string><string>$PORT</string>
    <string>--dir</string><string>$LOGS</string>
    <string>--quiet</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardErrorPath</key><string>$LOGS/probe-service.err</string>
</dict>
</plist>
EOF
  launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
  launchctl bootstrap "gui/$(id -u)" "$PLIST"
  echo "Installed: launchctl print gui/$(id -u)/com.ogxmini.probe"
fi
echo "Session files go to $LOGS. Turn on the adapter's Activity log in the web app."
