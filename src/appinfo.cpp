#include "appinfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <pwd.h>
#include <sys/types.h>

namespace {

// Runs a short helper command; returns stdout or an empty string on failure.
QString run(const QString &program, const QStringList &args, int timeoutMs = 2000)
{
    const QString exe = QStandardPaths::findExecutable(program);
    if (exe.isEmpty())
        return {};
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(exe, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QString stripEmail(QString s)
{
    const int lt = s.indexOf(QLatin1Char('<'));
    if (lt >= 0)
        s.truncate(lt);
    return s.trimmed();
}

QString distroName()
{
    QFile f(QStringLiteral("/etc/os-release"));
    if (f.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("NAME="))) {
                QString v = line.mid(5).trimmed();
                if (v.size() >= 2 && (v.startsWith(QLatin1Char('"')) || v.startsWith(QLatin1Char('\''))))
                    v = v.mid(1, v.size() - 2);
                return v;
            }
        }
    }
    return QStringLiteral("Linux");
}

struct PackageOwner {
    QString package;
    QString vendor;
};

// Which installed package owns `path`? Works with dpkg, rpm and pacman.
PackageOwner owningPackage(const QString &path)
{
    PackageOwner owner;
    if (path.isEmpty())
        return owner;

    QStringList candidates{path};
    const QString canon = QFileInfo(path).canonicalFilePath();
    if (!canon.isEmpty() && canon != path)
        candidates << canon;
    // usr-merged systems sometimes register /bin/foo instead of /usr/bin/foo
    for (const QString &c : QStringList(candidates)) {
        if (c.startsWith(QLatin1String("/usr/")))
            candidates << c.mid(4);
    }

    if (!QStandardPaths::findExecutable(QStringLiteral("dpkg-query")).isEmpty()) {
        for (const QString &c : candidates) {
            const QString out = run(QStringLiteral("dpkg-query"), {QStringLiteral("-S"), c});
            if (out.isEmpty())
                continue;
            const QString line = out.section(QLatin1Char('\n'), 0, 0);
            const int sep = line.indexOf(QLatin1String(": /"));
            if (sep <= 0)
                continue;
            owner.package = line.left(sep).section(QLatin1Char(','), 0, 0).trimmed();
            owner.vendor = stripEmail(run(QStringLiteral("dpkg-query"),
                                          {QStringLiteral("-W"), QStringLiteral("-f=${Maintainer}"), owner.package}));
            return owner;
        }
    }

    if (!QStandardPaths::findExecutable(QStringLiteral("rpm")).isEmpty()) {
        for (const QString &c : candidates) {
            const QString out = run(QStringLiteral("rpm"),
                                    {QStringLiteral("-qf"), QStringLiteral("--qf"),
                                     QStringLiteral("%{NAME}\\t%{VENDOR}\\n"), c});
            if (out.isEmpty())
                continue;
            const QString line = out.section(QLatin1Char('\n'), 0, 0);
            owner.package = line.section(QLatin1Char('\t'), 0, 0);
            owner.vendor = line.section(QLatin1Char('\t'), 1);
            if (owner.vendor == QLatin1String("(none)"))
                owner.vendor.clear();
            return owner;
        }
    }

    if (!QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        for (const QString &c : candidates) {
            const QString out = run(QStringLiteral("pacman"), {QStringLiteral("-Qqo"), c});
            if (out.isEmpty())
                continue;
            owner.package = out.section(QLatin1Char('\n'), 0, 0);
            return owner;
        }
    }
    return owner;
}

QString originFor(const QString &path)
{
    if (path.isEmpty())
        return QStringLiteral("This computer");
    if (path.startsWith(QLatin1String("/tmp/.mount_")))
        return QStringLiteral("AppImage on this computer");
    if (path.startsWith(QLatin1String("/media/")) || path.startsWith(QLatin1String("/run/media/"))
        || path.startsWith(QLatin1String("/mnt/")))
        return QStringLiteral("Removable or mounted drive");
    return QStringLiteral("Hard drive on this computer");
}

bool isInterpreter(const QString &exePath)
{
    static const QRegularExpression re(QStringLiteral(
        "^(python[0-9.]*|perl[0-9.]*|ruby[0-9.]*|node|nodejs|bash|sh|dash|zsh|fish|lua[0-9.]*|php[0-9.]*)$"));
    return re.match(QFileInfo(exePath).fileName()).hasMatch();
}

// First argument that is not an option, after argv[0].
QString scriptArgument(const QStringList &argv, const QString &cwd)
{
    for (int i = 1; i < argv.size(); ++i) {
        const QString &a = argv.at(i);
        if (a.startsWith(QLatin1Char('-')))
            continue;
        const QString path = QDir::isAbsolutePath(a) ? a : QDir(cwd).absoluteFilePath(a);
        if (QFileInfo(path).isFile())
            return QDir::cleanPath(path);
        return {};
    }
    return {};
}

} // namespace

UacUser lookupUser(const QString &name)
{
    UacUser u;
    u.name = name;
    QString home;
    if (const passwd *pw = getpwnam(name.toLocal8Bit().constData())) {
        u.fullName = QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
        home = QString::fromLocal8Bit(pw->pw_dir);
    }
    const QStringList avatars{
        QStringLiteral("/var/lib/AccountsService/icons/") + name,
        home + QStringLiteral("/.face.icon"),
        home + QStringLiteral("/.face"),
    };
    for (const QString &a : avatars) {
        if (!home.isEmpty() || a.startsWith(QLatin1String("/var"))) {
            if (QFileInfo(a).isReadable() && QFileInfo(a).isFile()) {
                u.avatarPath = a;
                break;
            }
        }
    }
    return u;
}

UacInfo describeRequest(const QString &actionId,
                        const QString &message,
                        const QMap<QString, QString> &details)
{
    UacInfo info;
    info.actionId = actionId;
    info.message = message;
    info.commandLine = details.value(QStringLiteral("command_line"));

    QString exe = details.value(QStringLiteral("program")); // set by pkexec
    QStringList argv;
    QString cwd = QDir::rootPath();

    if (!exe.isEmpty()) {
        argv = QProcess::splitCommand(info.commandLine);
    } else {
        // Any other action: describe the process that asked for it.
        const QString pid = details.value(QStringLiteral("polkit.subject-pid"));
        if (!pid.isEmpty()) {
            const QString proc = QStringLiteral("/proc/") + pid;
            exe = QFileInfo(proc + QStringLiteral("/exe")).symLinkTarget();
            cwd = QFileInfo(proc + QStringLiteral("/cwd")).symLinkTarget();
            QFile cmd(proc + QStringLiteral("/cmdline"));
            if (cmd.open(QIODevice::ReadOnly)) {
                const QList<QByteArray> parts = cmd.readAll().split('\0');
                for (const QByteArray &p : parts) {
                    if (!p.isEmpty())
                        argv << QString::fromLocal8Bit(p);
                }
            }
            exe.remove(QLatin1String(" (deleted)"));
        }
    }

    // "python3 ./install.py" should be described as install.py, not python3.
    QString target = exe;
    if (!exe.isEmpty() && isInterpreter(exe)) {
        const QString script = scriptArgument(argv, cwd);
        if (!script.isEmpty()) {
            info.interpreter = exe;
            target = script;
        }
    }

    if (target.isEmpty()) {
        info.appName = message.isEmpty() ? actionId : message;
        info.origin = originFor({});
        info.location = QStringLiteral("Unknown");
    } else {
        info.appName = QFileInfo(target).fileName();
        info.origin = originFor(target);
        info.location = target;
    }

    const PackageOwner owner = owningPackage(target);
    info.verified = !owner.package.isEmpty();
    if (info.verified) {
        const QString vendor = owner.vendor.isEmpty() ? distroName() : owner.vendor;
        info.publisher = vendor;
        info.package = owner.package;
    } else {
        info.publisher = QStringLiteral("Unknown");
    }
    return info;
}
