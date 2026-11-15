#include "agent.h"
#include "sound.h"
#include "uacprompt.h"

#include <PolkitQt1/Subject>

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>

#include <cstdio>
#include <unistd.h>

namespace {

UacInfo sampleInfo(bool verified)
{
    UacInfo info;
    info.verified = verified;
    if (verified) {
        info.appName = QStringLiteral("plasma-discover");
        info.publisher = QStringLiteral("Kubuntu Developers");
        info.package = QStringLiteral("plasma-discover");
        info.location = QStringLiteral("/usr/bin/plasma-discover");
        info.actionId = QStringLiteral("org.freedesktop.packagekit.package-install");
        info.message = QStringLiteral("Authentication is required to install software");
    } else {
        info.appName = QStringLiteral("install-driver.run");
        info.publisher = QStringLiteral("Unknown");
        info.location = QDir::homePath() + QStringLiteral("/Downloads/install-driver.run");
        info.actionId = QStringLiteral("org.freedesktop.policykit.exec");
        info.message = QStringLiteral("Authentication is needed to run `install-driver.run' as the super user");
        info.commandLine = info.location + QStringLiteral(" --install");
    }
    info.origin = QStringLiteral("Hard drive on this computer");
    info.users << lookupUser(QString::fromLocal8Bit(qgetenv("USER")).isEmpty()
                                 ? QStringLiteral("user")
                                 : QString::fromLocal8Bit(qgetenv("USER")));
    return info;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("uacwin-agent"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setDesktopFileName(QStringLiteral("uacwin-agent"));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windows-style polkit authentication agent for KDE"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption preview(QStringLiteral("preview"), QStringLiteral("Show a sample prompt without polkit."));
    QCommandLineOption verified(QStringLiteral("verified"), QStringLiteral("With --preview/--grab: use the verified-publisher (blue) look."));
    QCommandLineOption grab(QStringLiteral("grab"), QStringLiteral("Render a sample prompt to an image file and exit."), QStringLiteral("file"));
    QCommandLineOption theme(QStringLiteral("theme"), QStringLiteral("Force 'dark' or 'light' (default: follow KDE)."), QStringLiteral("name"));
    QCommandLineOption consent(QStringLiteral("consent"), QStringLiteral("With --preview/--grab: Yes/No only, no password box."));
    QCommandLineOption whichSound(QStringLiteral("which-sound"), QStringLiteral("Print the sound file the prompt would play and exit."));
    QCommandLineOption noDim(QStringLiteral("no-dim"), QStringLiteral("Do not dim the screen behind the prompt."));
    parser.addOptions({preview, verified, grab, theme, consent, whichSound, noDim});
    parser.process(app);

    QString themeName = parser.value(theme);
    if (themeName.isEmpty())
        themeName = QString::fromLocal8Bit(qgetenv("UACWIN_THEME"));
    const UacTheme uacTheme = UacTheme::detect(themeName);
    const bool dim = !parser.isSet(noDim) && qgetenv("UACWIN_NO_DIM").isEmpty();

    if (parser.isSet(whichSound)) {
        const QString f = UacSound::resolvePromptSound();
        printf("%s\n", f.isEmpty() ? "(no sound)" : qPrintable(f));
        return 0;
    }

    UacInfo sample = sampleInfo(parser.isSet(verified));
    sample.consent = parser.isSet(consent);

    if (parser.isSet(grab)) {
        const QImage img = UacPrompt::renderPreview(sample, uacTheme, QSize(900, 680));
        return img.save(parser.value(grab)) ? 0 : 1;
    }

    if (parser.isSet(preview)) {
        auto *prompt = new UacPrompt(sample, uacTheme, dim);
        UacCard *card = prompt->card();
        if (!sample.consent)
            card->setPrompt(QStringLiteral("Password:"), false);
        QObject::connect(card, &UacCard::accepted, card, [card, prompt](const QString &pw) {
            if (pw == QLatin1String("wrong")) {
                card->clearPassword();
                card->setError(QStringLiteral("The password is incorrect. Try again."));
                card->shake();
                card->setPrompt(QStringLiteral("Password:"), false);
                return;
            }
            qInfo("Preview: Yes");
            prompt->close();
            QCoreApplication::quit();
        });
        QObject::connect(card, &UacCard::rejected, card, [prompt] {
            qInfo("Preview: No");
            prompt->close();
            QCoreApplication::quit();
        });
        prompt->show();
        UacSound::playPromptSound();
        return app.exec();
    }

    UacAgent agent(uacTheme, dim);
    PolkitQt1::UnixSessionSubject session(getpid());
    if (!agent.registerListener(session, QStringLiteral("/org/uacwin/PolicyKit1/AuthenticationAgent"))) {
        qCritical("uacwin: could not register as polkit agent. Is another agent "
                  "(e.g. polkit-kde-authentication-agent-1) still running?");
        return 1;
    }
    qInfo("uacwin: polkit agent registered");
    return app.exec();
}
