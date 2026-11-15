#pragma once

#include <QList>
#include <QMap>
#include <QString>

struct UacUser {
    QString name;       // login name, e.g. "patch"
    QString fullName;   // GECOS name, may be empty
    QString avatarPath; // may be empty
};

// Everything the prompt needs to show for one authentication request.
struct UacInfo {
    bool verified = false;  // true = owned by an installed package (blue header)
    QString appName;        // big line under the header
    QString publisher;      // "Ubuntu Developers" or "Unknown"
    QString package;        // owning package, if any
    QString origin;         // "Hard drive on this computer", ...
    QString location;       // full path of the program
    QString interpreter;    // set when the "program" is a script run by e.g. python3
    QString actionId;       // polkit action id
    QString message;        // polkit's own message for the action
    QString commandLine;    // pkexec command line, if any
    QList<UacUser> users;   // accounts that may authenticate
    int selectedUser = 0;
};

// Builds a UacInfo from a polkit request. `details` are polkit's key/value
// details (program, command_line, polkit.subject-pid, ...).
UacInfo describeRequest(const QString &actionId,
                        const QString &message,
                        const QMap<QString, QString> &details);

// Looks up display data for a local account.
UacUser lookupUser(const QString &name);
