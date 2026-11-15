#!/usr/bin/env bash
# Turns Yes/No-only mode off again: polkit prompts ask for your password.
set -euo pipefail

PAM=/etc/pam.d/polkit-1

if [[ ! -f $PAM ]] || ! grep -q "^# uacwin-consent begin" "$PAM"; then
    echo "Yes/No-only mode is not on."
    exit 0
fi

if grep -q "^# uacwin-consent begin created-file" "$PAM"; then
    # UACWin created this file from the distro default; remove it entirely.
    sudo rm -f "$PAM"
else
    sudo sed -i '/^# uacwin-consent begin/,/^# uacwin-consent end/d' "$PAM"
fi

systemctl --user restart uacwin-agent.service 2>/dev/null || true
echo "Off. Admin prompts ask for your password again."
