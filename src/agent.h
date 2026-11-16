#pragma once

#include "uacprompt.h"

#include <PolkitQt1/Agent/Listener>
#include <PolkitQt1/Agent/Session>
#include <PolkitQt1/Identity>

#include <QPointer>

// polkit authentication agent that shows the Windows-style prompt.
class UacAgent : public PolkitQt1::Agent::Listener
{
    Q_OBJECT
public:
    UacAgent(const UacTheme &theme, UacBackdrop backdrop, QObject *parent = nullptr);

public Q_SLOTS:
    void initiateAuthentication(const QString &actionId,
                                const QString &message,
                                const QString &iconName,
                                const PolkitQt1::Details &details,
                                const QString &cookie,
                                const PolkitQt1::Identity::List &identities,
                                PolkitQt1::Agent::AsyncResult *result) override;
    bool initiateAuthenticationFinish() override;
    void cancelAuthentication() override;

private:
    void startSession();
    void stopSession();
    void onCompleted(bool gainedAuthorization);
    void finish();

    UacTheme m_theme;
    UacBackdrop m_backdrop = UacBackdrop::Wallpaper;

    bool m_inProgress = false;
    int m_failures = 0;
    QString m_cookie;
    PolkitQt1::Agent::AsyncResult *m_result = nullptr;
    PolkitQt1::Identity::List m_identities;
    int m_selected = 0;
    QPointer<PolkitQt1::Agent::Session> m_session;
    QPointer<UacPrompt> m_prompt;
};
