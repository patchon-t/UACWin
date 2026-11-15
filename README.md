# UACWin

A Windows UAC–style admin prompt for KDE Plasma on Linux.

UACWin replaces KDE's polkit password dialog with a prompt modeled on Windows User Account Control:
the screen dims, and a card asks *"Do you want to allow this app to make changes to your device?"*
with **Yes** / **No**.

| Unknown publisher (dark) | Verified publisher (dark) | Light theme |
|---|---|---|
| ![](docs/unknown-dark.png) | ![](docs/verified-dark.png) | ![](docs/unknown-light.png) |

## What it does

- **Dims every monitor** and shows the prompt in the middle of the screen.
- **Amber header** for programs that no installed package owns (downloaded binaries, scripts, AppImages).
- **Blue header with "Verified publisher"** for programs installed by your package manager
  (`dpkg`, `rpm` or `pacman`). The publisher is the package's maintainer or vendor.
- **Show more details** reveals the program location, package, command line, polkit's reason and the action ID.
- For scripts (`pkexec python3 ./setup.py`) it names the script, not `python3`.
- It still asks for your **password**, the same way Windows asks for credentials when the
  "unknown publisher" prompt appears. Fingerprint login works too if you have set it up.
- Follows KDE's light/dark theme and uses your KDE accent color for the Yes button and links.
- Three wrong passwords cancel the request. Esc, ✕ or **No** cancel it immediately.

## Install

### 1. Build dependencies

Kubuntu 24.04 / Debian 12 (Plasma 5):

```bash
sudo apt install build-essential cmake qtbase5-dev libpolkit-qt5-1-dev
```

Kubuntu 25.04+ / Debian 13 (Plasma 6):

```bash
sudo apt install build-essential cmake qt6-base-dev libpolkit-qt6-1-dev
```

Fedora KDE:

```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel polkit-qt6-1-devel
```

Arch / EndeavourOS / Manjaro:

```bash
sudo pacman -S --needed base-devel cmake qt6-base polkit-qt6
```

### 2. Build, install and switch agents

Run as your normal user (not with `sudo`):

```bash
./scripts/install.sh
```

The script builds the agent, installs it to `/usr/local`, turns off KDE's polkit agent **for your user only**,
and starts UACWin as a systemd user service. If UACWin fails to start, the script puts KDE's agent back.

### 3. Try it

```bash
pkexec true
```

### Preview without installing

```bash
./build/uacwin-agent --preview             # amber "unknown publisher" prompt
./build/uacwin-agent --preview --verified  # blue "verified" prompt
```

In preview mode, typing `wrong` shows the wrong-password state. Anything else closes it.

## Uninstall

```bash
./scripts/uninstall.sh
```

This stops UACWin, turns KDE's agent back on, and removes the installed files.
If the prompt ever stops appearing, `sudo` in a terminal still works, so you can always run the uninstall script.

## Options

| Flag / env var | Effect |
|---|---|
| `--theme dark\|light` or `UACWIN_THEME=` | Force a theme instead of following KDE |
| `--no-dim` or `UACWIN_NO_DIM=1` | Show only the card, without dimming the screen |

To set an env var for the service: `systemctl --user edit uacwin-agent`, then add

```ini
[Service]
Environment=UACWIN_THEME=dark
```

## Logs

```bash
journalctl --user -u uacwin-agent -f
```

## Limitations

- Windows draws UAC on a separate "secure desktop" that other apps can't see or click.
  UACWin is a normal window on your desktop, so it **always asks for a password** and never offers a password-free "Yes".
- "Verified publisher" only means an installed package owns the file. Unlike Windows, it does not check a code signature.
- On Wayland, KWin decides which window gets focus. If the prompt opens without keyboard focus, click it.
