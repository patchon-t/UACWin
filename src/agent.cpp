#include "agent.h"
#include "sound.h"

#include <PolkitQt1/Details>

#include <QDebug>
#include <QFile>

#include <pwd.h>
#include <unistd.h>

using PolkitQt1::Agent::Session;

namespace {

// polkit identities look like "unix-user:alice" (or a numeric uid).
QString userNameOf(const PolkitQt1::Identity &identity)
{
    const QString s = identity.toString();
    if (!s.startsWith(QLatin1String("unix-user:")))
        return {};
    const QString v = s.mid(10);
    bool numeric = false;
    const uint uid = v.toUInt(&numeric);
    if (numeric) {
        if (const passwd *pw = getpwuid(uid))
            return QString::fromLocal8Bit(pw->pw_name);
    }
    return v;
}

// Yes/No-only mode is on when scripts/enable-no-password.sh added its PAM
// rule (or UACWIN_MODE=consent). Without that rule PAM would still ask for a
// password, so the prompt falls back to showing the password box.
bool consentEnabled()
{
    const QString mode = qEnvironmentVariable("UACWIN_MODE");
    if (mode == QLatin1String("password"))
        return false;
    if (mode == QLatin1String("consent"))
        return true;
    QFile pam(QStringLiteral("/etc/pam.d/polkit-1"));
    return pam.open(QIODevice::ReadOnly) && pam.readAll().contains("# uacwin-consent begin");
}

QString currentUserName()
{
    if (const passwd *pw = getpwuid(getuid()))
        return QString::fromLocal8Bit(pw->pw_name);
    return {};
}

} // namespace

UacAgent::UacAgent(const UacTheme &theme, bool dim, QObject *parent)
    : PolkitQt1::Agent::Listener(parent)
    , m_theme(theme)
    , m_dim(dim)
{
}

void UacAgent::initiateAuthentication(const QString &actionId,
                                      const QString &message,
                                      const QString &iconName,
                                      const PolkitQt1::Details &details,
                                      const QString &cookie,
                                      const PolkitQt1::Identity::List &identities,
                                      PolkitQt1::Agent::AsyncResult *result)
{
    Q_UNUSED(iconName)

    if (m_inProgress) {
        result->setError(QStringLiteral("Another authentication is already in progress."));
        result->setCompleted();
        return;
    }

    m_inProgress = true;
    m_failures = 0;
    m_cookie = cookie;
    m_result = result;

    QMap<QString, QString> detailMap;
    const QStringList keys = details.keys();
    for (const QString &k : keys)
        detailMap.insert(k, details.lookup(k));

    UacInfo info = describeRequest(actionId, message, detailMap);

    // Only unix users can type a password; prefer the logged-in user.
    m_identities.clear();
    const QString me = currentUserName();
    m_selected = 0;
    for (const PolkitQt1::Identity &id : identities) {
        const QString name = userNameOf(id);
        if (name.isEmpty())
            continue;
        if (name == me)
            m_selected = m_identities.size();
        m_identities << id;
        info.users << lookupUser(name);
    }
    info.selectedUser = m_selected;
    info.consent = consentEnabled();

    if (m_identities.isEmpty()) {
        qWarning() << "uacwin: no usable identity for" << actionId;
        m_result->setError(QStringLiteral("No user account can authenticate this action."));
        m_result->setCompleted();
        m_result = nullptr;
        m_inProgress = false;
        return;
    }

    m_prompt = new UacPrompt(info, m_theme, m_dim, this);
    UacCard *card = m_prompt->card();

    connect(card, &UacCard::accepted, this, [this](const QString &response) {
        if (m_session)
            m_session->setResponse(response);
        else
            startSession(); // Yes in Yes/No-only mode: now ask polkit
    });
    connect(card, &UacCard::rejected, this, [this] {
        stopSession();
        finish();
    });
    connect(card, &UacCard::userChanged, this, [this](int index) {
        if (index < 0 || index >= m_identities.size())
            return;
        m_selected = index;
        m_failures = 0;
        stopSession();
        if (!m_prompt->card()->isConsent())
            startSession();
    });

    m_prompt->show();
    UacSound::playPromptSound();
    if (!info.consent)
        startSession();
}

bool UacAgent::initiateAuthenticationFinish()
{
    return true;
}

void UacAgent::cancelAuthentication()
{
    // The requesting program went away or cancelled.
    if (!m_inProgress)
        return;
    stopSession();
    finish();
}

void UacAgent::startSession()
{
    m_session = new Session(m_identities.at(m_selected), m_cookie, m_result, this);

    connect(m_session, &Session::request, this, [this](const QString &prompt, bool echo) {
        if (m_prompt)
            m_prompt->card()->requirePassword(prompt, echo);
    });
    connect(m_session, &Session::showError, this, [this](const QString &text) {
        if (m_prompt)
            m_prompt->card()->setError(text);
    });
    connect(m_session, &Session::showInfo, this, [this](const QString &text) {
        // e.g. "Place your finger on the fingerprint reader"
        if (m_prompt)
            m_prompt->card()->setInfo(text);
    });
    connect(m_session, &Session::completed, this, &UacAgent::onCompleted);

    m_session->initiate();
}

void UacAgent::stopSession()
{
    if (!m_session)
        return;
    // Disconnect first: cancel() emits completed(false) synchronously.
    disconnect(m_session, nullptr, this, nullptr);
    m_session->cancel();
    m_session->deleteLater();
    m_session = nullptr;
}

void UacAgent::onCompleted(bool gainedAuthorization)
{
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
        m_session->deleteLater();
        m_session = nullptr;
    }

    if (gainedAuthorization) {
        finish();
        return;
    }

    ++m_failures;
    if (m_failures >= 3) {
        finish();
        return;
    }
    if (m_prompt && m_prompt->card()->isConsent()) {
        // PAM refused without asking for anything (e.g. you are not in the admin group).
        UacCard *card = m_prompt->card();
        card->setError(QStringLiteral("Your account is not allowed to approve this."));
        card->setBusy(false);
        card->shake();
        return; // clicking Yes again retries
    }
    if (m_prompt) {
        UacCard *card = m_prompt->card();
        card->clearPassword();
        card->setError(QStringLiteral("The password is incorrect. Try again."));
        card->shake();
    }
    startSession();
}

void UacAgent::finish()
{
    if (m_prompt) {
        m_prompt->close();
        m_prompt->deleteLater();
        m_prompt = nullptr;
    }
    if (m_result) {
        m_result->setCompleted();
        m_result = nullptr;
    }
    m_identities.clear();
    m_inProgress = false;
}
