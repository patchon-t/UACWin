#!/usr/bin/env bash
# Builds and installs UACWin, then swaps it in for KDE's polkit agent.
# Run as your normal user (it calls sudo only for the install step).
set -euo pipefail

if [[ $EUID -eq 0 ]]; then
    echo "Run this as your normal user, not root (it will ask for sudo when needed)." >&2
    exit 1
fi

cd "$(dirname "$0")/.."
PREFIX="${PREFIX:-/usr/local}"

echo "==> Building"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build -j"$(nproc)"

echo "==> Installing to $PREFIX (needs sudo)"
sudo cmake --install build

echo "==> Turning off KDE's own polkit agent for your user"
# Plasma (5.25+ with systemd startup, and Plasma 6) starts it as a user unit.
systemctl --user mask plasma-polkit-agent.service >/dev/null 2>&1 || true
systemctl --user stop plasma-polkit-agent.service >/dev/null 2>&1 || true
# Older/non-systemd startup uses an XDG autostart entry; hide it for this user only.
for f in /etc/xdg/autostart/polkit-kde-authentication-agent-1.desktop \
         /etc/xdg/autostart/org.kde.polkit-kde-authentication-agent-1.desktop; do
    if [[ -f "$f" ]]; then
        mkdir -p "$HOME/.config/autostart"
        printf '[Desktop Entry]\nType=Application\nName=%s\nHidden=true\nX-UACWin-Override=true\n' \
            "$(basename "$f" .desktop)" > "$HOME/.config/autostart/$(basename "$f")"
    fi
done
pkill -f polkit-kde-authentication-agent-1 >/dev/null 2>&1 || true

echo "==> Starting UACWin"
systemctl --user daemon-reload
systemctl --user enable --now uacwin-agent.service

sleep 1
if systemctl --user is-active --quiet uacwin-agent.service; then
    echo
    echo "Done. Try it:   pkexec true"
    echo "Undo anytime:   ./scripts/uninstall.sh"
else
    echo
    echo "UACWin did not start. Logs: journalctl --user -u uacwin-agent -n 50" >&2
    echo "Restoring KDE's agent so you are not left without one..." >&2
    "$(dirname "$0")/uninstall.sh" --keep-files || true
    exit 1
fi
