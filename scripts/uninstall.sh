#!/usr/bin/env bash
# Stops UACWin and gives KDE its own polkit agent back.
#   --keep-files   only switch agents back, don't delete installed files
set -uo pipefail

if [[ $EUID -eq 0 ]]; then
    echo "Run this as your normal user, not root." >&2
    exit 1
fi

cd "$(dirname "$0")/.."
PREFIX="${PREFIX:-/usr/local}"

echo "==> Stopping UACWin"
systemctl --user disable --now uacwin-agent.service >/dev/null 2>&1 || true

echo "==> Restoring KDE's polkit agent"
systemctl --user unmask plasma-polkit-agent.service >/dev/null 2>&1 || true
for f in "$HOME"/.config/autostart/polkit-kde-authentication-agent-1.desktop \
         "$HOME"/.config/autostart/org.kde.polkit-kde-authentication-agent-1.desktop; do
    if [[ -f "$f" ]] && grep -q '^X-UACWin-Override=true' "$f"; then
        rm -f "$f"
    fi
done
systemctl --user daemon-reload
if ! systemctl --user start plasma-polkit-agent.service >/dev/null 2>&1; then
    # Not a systemd-started Plasma: launch the agent directly.
    for agent in /usr/lib/*/libexec/polkit-kde-authentication-agent-1 \
                 /usr/libexec/polkit-kde-authentication-agent-1 \
                 /usr/lib/polkit-kde-authentication-agent-1; do
        if [[ -x "$agent" ]]; then
            (setsid "$agent" >/dev/null 2>&1 &)
            break
        fi
    done
fi

if [[ "${1:-}" != "--keep-files" ]]; then
    echo "==> Removing installed files (needs sudo)"
    if [[ -f build/install_manifest.txt ]]; then
        xargs -d '\n' sudo rm -f < build/install_manifest.txt
    else
        sudo rm -f "$PREFIX/libexec/uacwin-agent" "$PREFIX/lib/systemd/user/uacwin-agent.service"
    fi
    systemctl --user daemon-reload
fi

echo "Done. KDE's normal password dialog is back."
