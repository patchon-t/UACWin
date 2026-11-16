#pragma once

#include <QHash>
#include <QString>

// Minimal INI reader for KDE config files. KDE uses section names like
// [Event/authenticate] or [Containments][1][Wallpaper][org.kde.image][General]
// which QSettings misreads, so they are parsed by hand. A section header
// "[A][B]" is stored under the key "A][B".
using IniSections = QHash<QString, QHash<QString, QString>>;

IniSections readIni(const QString &path);
