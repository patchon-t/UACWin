#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace UacWallpaper {

// Image file of the KDE desktop wallpaper on a Plasma screen (0 = primary),
// read from plasma-org.kde.plasma.desktop-appletsrc. Picks the size that best
// fits `targetPx` from wallpaper packages, and the dark variant when `dark`.
// Override with UACWIN_WALLPAPER=/path/to/image. Empty if none is found.
QString desktopWallpaper(int plasmaScreen, bool dark, const QSize &targetPx);

// Blurred, slightly darkened version of `sharp`, cropped to fill `targetPx`.
QImage blur(const QImage &sharp, const QSize &targetPx);

// Loads `path` and returns blur() of it. Results are cached per file and size.
QImage blurredFile(const QString &path, const QSize &targetPx);

} // namespace UacWallpaper
