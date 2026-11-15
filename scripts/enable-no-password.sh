#!/usr/bin/env bash
# Turns on Yes/No-only mode: members of your admin group (sudo/wheel) approve
# polkit prompts by clicking Yes, without typing a password.
#
# How: adds one rule at the top of /etc/pam.d/polkit-1 that lets admin-group
# members pass polkit's password check. `sudo` in a terminal is NOT affected.
# UACWin waits for you to click Yes before it asks polkit, so nothing is
# approved without a click.
#
# Undo with ./scripts/disable-no-password.sh (uninstall.sh also undoes it).
set -euo pipefail

if [[ $EUID -eq 0 ]]; then
    echo "Run this as your normal user (it will ask for sudo)." >&2
    exit 1
fi

PAM=/etc/pam.d/polkit-1
MARK_BEGIN="# uacwin-consent begin"
MARK_END="# uacwin-consent end"

if [[ -f $PAM ]] && grep -q "^$MARK_BEGIN" "$PAM"; then
    echo "Already on."
    exit 0
fi

group=""
for g in sudo wheel admin; do
    if getent group "$g" >/dev/null && id -nG "$USER" | tr ' ' '\n' | grep -qx "$g"; then
        group=$g
        break
    fi
done
if [[ -z $group ]]; then
    echo "Your account is not in an admin group (sudo, wheel or admin), so this can't be turned on." >&2
    exit 1
fi

if [[ -f $PAM ]]; then
    src=$PAM
    created=""
elif [[ -f /usr/lib/pam.d/polkit-1 ]]; then
    src=/usr/lib/pam.d/polkit-1
    created=" created-file"
elif [[ -f /usr/share/pam.d/polkit-1 ]]; then
    src=/usr/share/pam.d/polkit-1
    created=" created-file"
else
    echo "Could not find polkit's PAM file (polkit-1). Nothing changed." >&2
    exit 1
fi

cat <<EOF
This lets anyone in the '$group' group approve admin prompts with one click.

What that means:
  * No password for GUI admin prompts (Discover, Partition Manager, pkexec...).
  * Any program running as you could also get admin rights without your
    password, by replacing or clicking the prompt. Windows UAC has the same
    weakness when you use an admin account.
  * 'sudo' in a terminal still asks for your password.

EOF
read -r -p "Turn on Yes/No-only mode? [y/N] " answer
[[ $answer == [yY]* ]] || { echo "Nothing changed."; exit 0; }

tmp=$(mktemp)
{
    echo "$MARK_BEGIN$created (added by UACWin: admins approve with the Yes button)"
    echo "auth    sufficient    pam_succeed_if.so quiet user ingroup $group"
    echo "$MARK_END"
    cat "$src"
} > "$tmp"
sudo install -m 0644 -o root -g root "$tmp" "$PAM"
rm -f "$tmp"

systemctl --user restart uacwin-agent.service 2>/dev/null || true
echo "On. Try: pkexec true"
echo "Undo:    ./scripts/disable-no-password.sh"
