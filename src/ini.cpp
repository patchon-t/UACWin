#include "ini.h"

#include <QFile>
#include <QStringList>

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
