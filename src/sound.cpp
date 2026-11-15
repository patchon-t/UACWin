#include "sound.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

namespace {

using IniSections = QHash<QString, QHash<QString, QString>>;

// Minimal INI reader. KDE files use section names like [Event/authenticate],
// which QSettings would misread, so parse them by hand.
IniSections readIni(const QString &path)
{
    IniSections out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QString section;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        QString key = line.left(eq).trimmed();
        key.remove(QLatin1String("[$e]"));
        key.remove(QLatin1String("[$i]"));
        out[section][key] = line.mid(eq + 1).trimmed();
    }
    return out;
}

QString configHome()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QStringList dataDirs()
{
    return QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
}

QString soundThemeName()
{
    // Plasma 6: System Settings > Colors & Themes > Sound theme
    QString theme = readIni(configHome() + QStringLiteral("/kdeglobals"))
                        .value(QStringLiteral("Sounds")).value(QStringLiteral("Theme"));
    if (theme.isEmpty()) {
        theme = readIni(configHome() + QStringLiteral("/gtk-3.0/settings.ini"))
                    .value(QStringLiteral("Settings")).value(QStringLiteral("gtk-sound-theme-name"));
    }
    if (theme.isEmpty())
        theme = QStringLiteral("ocean");
    return theme;
}

const QStringList kExtensions{QStringLiteral("oga"), QStringLiteral("ogg"), QStringLiteral("wav"),
                              QStringLiteral("flac")};

// Looks for `name` in one theme and the themes it inherits from.
QString searchTheme(const QString &theme, const QString &name, QSet<QString> &visited)
{
    if (theme.isEmpty() || visited.contains(theme))
        return {};
    visited.insert(theme);

    QStringList inherits;
    for (const QString &dir : dataDirs()) {
        const QString base = dir + QStringLiteral("/sounds/") + theme;
        if (!QFileInfo(base).isDir())
            continue;
        const auto index = readIni(base + QStringLiteral("/index.theme")).value(QStringLiteral("Sound Theme"));
        QStringList subdirs = index.value(QStringLiteral("Directories")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (subdirs.isEmpty())
            subdirs << QStringLiteral("stereo");
        subdirs << QString(); // some themes keep files at the top level
        for (const QString &sub : subdirs) {
            for (const QString &ext : kExtensions) {
                const QString file = QDir::cleanPath(base + QLatin1Char('/') + sub.trimmed() + QLatin1Char('/')
                                                     + name + QLatin1Char('.') + ext);
                if (QFileInfo(file).isFile())
                    return file;
            }
        }
        if (inherits.isEmpty())
            inherits = index.value(QStringLiteral("Inherits")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    }
    for (const QString &parent : inherits) {
        const QString found = searchTheme(parent.trimmed(), name, visited);
        if (!found.isEmpty())
            return found;
    }
    return {};
}

// Sound naming spec lookup: try the name, then strip "-suffix" parts
// ("dialog-warning-auth" -> "dialog-warning" -> "dialog").
QString lookupThemeSound(const QString &name)
{
    const QString theme = soundThemeName();
    QString n = name;
    while (!n.isEmpty()) {
        QSet<QString> visited;
        QString found = searchTheme(theme, n, visited);
        if (found.isEmpty())
            found = searchTheme(QStringLiteral("freedesktop"), n, visited);
        if (!found.isEmpty())
            return found;
        const int dash = n.lastIndexOf(QLatin1Char('-'));
        if (dash <= 0)
            break;
        n.truncate(dash);
    }
    return {};
}

// Turns a "Sound=" value (absolute path, file in share/sounds, or theme name) into a file.
QString resolveSpec(QString spec)
{
    spec = spec.trimmed();
    if (spec.startsWith(QLatin1String("file://")))
        spec = spec.mid(7);
    if (spec.isEmpty())
        return {};
    if (QDir::isAbsolutePath(spec))
        return QFileInfo(spec).isFile() ? spec : QString();
    if (spec.contains(QLatin1Char('.'))) {
        for (const QString &dir : dataDirs()) {
            const QString file = dir + QStringLiteral("/sounds/") + spec;
            if (QFileInfo(file).isFile())
                return file;
        }
        return {};
    }
    return lookupThemeSound(spec);
}

QString notifyrcSound(const QString &path)
{
    return readIni(path).value(QStringLiteral("Event/authenticate")).value(QStringLiteral("Sound"));
}

} // namespace

QString UacSound::resolvePromptSound()
{
    const QString env = qEnvironmentVariable("UACWIN_SOUND").trimmed();
    if (env == QLatin1String("none") || env == QLatin1String("off") || env == QLatin1String("0"))
        return {};
    if (!env.isEmpty()) {
        const QString f = resolveSpec(env);
        if (!f.isEmpty())
            return f;
    }

    // What you picked for KDE's own authentication prompt.
    QStringList notifyrcs{configHome() + QStringLiteral("/policykit1-kde.notifyrc")};
    for (const QString &dir : dataDirs()) {
        notifyrcs << dir + QStringLiteral("/knotifications6/policykit1-kde.notifyrc")
                  << dir + QStringLiteral("/knotifications5/policykit1-kde.notifyrc");
    }
    for (const QString &rc : notifyrcs) {
        const QString f = resolveSpec(notifyrcSound(rc));
        if (!f.isEmpty())
            return f;
    }

    return lookupThemeSound(QStringLiteral("dialog-warning-auth"));
}

void UacSound::playPromptSound()
{
    const QString file = resolvePromptSound();
    if (file.isEmpty())
        return;

    struct Player {
        const char *exe;
        QStringList args;
    };
    const QList<Player> players{
        {"pw-play", {file}},
        {"paplay", {file}},
        {"canberra-gtk-play", {QStringLiteral("-f"), file}},
        {"ffplay", {QStringLiteral("-nodisp"), QStringLiteral("-autoexit"), QStringLiteral("-loglevel"),
                    QStringLiteral("quiet"), file}},
    };
    for (const Player &p : players) {
        const QString exe = QStandardPaths::findExecutable(QString::fromLatin1(p.exe));
        if (!exe.isEmpty()) {
            QProcess::startDetached(exe, p.args);
            return;
        }
    }
}
