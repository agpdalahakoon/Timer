#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/home/praveen/soft/Timer"
EXT_UUID="timer-panel@praveen.local"
EXT_DIR="$HOME/.local/share/gnome-shell/extensions/$EXT_UUID"

#sudo apt-get install cmake build-essential gcc g++ pkg-config libgtk-3-dev libx11-dev libxss-dev

cmake -S . -B build
cmake --build build

mkdir -p "$APP_DIR"
install -m 0755 build/timer "$APP_DIR/timer.new"
mv "$APP_DIR/timer.new" "$APP_DIR/timer"

mkdir -p "$EXT_DIR"
cp gnome-shell/$EXT_UUID/metadata.json "$EXT_DIR/metadata.json"
cp gnome-shell/$EXT_UUID/extension.js "$EXT_DIR/extension.js"
cp gnome-shell/$EXT_UUID/stylesheet.css "$EXT_DIR/stylesheet.css"

if command -v gnome-extensions >/dev/null 2>&1; then
    gnome-extensions enable "$EXT_UUID" || true
fi

echo "Installed timer backend to $APP_DIR/timer"
echo "Installed GNOME extension to $EXT_DIR"
echo "On X11, press Alt+F2, type r, and press Enter to reload GNOME Shell if the panel item is not visible."
