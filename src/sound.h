#pragma once

#include <QString>

namespace UacSound {

// Finds the sound file to play when the prompt opens, in this order:
//  1. $UACWIN_SOUND (a sound-theme name like "dialog-question", a file path, or "none")
//  2. the sound you set in KDE for the "Authentication System" notification
//     (~/.config/policykit1-kde.notifyrc, the original KDE agent's event)
//  3. the same event's default sound shipped by KDE, if any
//  4. "dialog-warning-auth" from your current sound theme (falls back to
//     dialog-warning, then the freedesktop theme)
// Returns an empty string when no sound should be played.
QString resolvePromptSound();

// Plays the prompt sound without blocking.
void playPromptSound();

} // namespace UacSound
