#pragma once

#include "appinfo.h"

#include <QColor>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

// What is shown behind the prompt.
enum class UacBackdrop {
    Wallpaper, // your KDE wallpaper, blurred (default)
    Dim,       // the current screen, darkened
    None,      // nothing: just the prompt window
};
UacBackdrop parseBackdrop(const QString &name); // "wallpaper", "dim", "none"

struct UacTheme {
    bool dark = true;
    QColor body, footer, border, text, subtext;
    QColor inputBg, inputBorder, neutralBtn, neutralBtnText, neutralBtnBorder;
    QColor error, accent, accentText, link;
    QColor bandVerified, bandUnknown;

    // override: "dark", "light" or empty to follow the desktop.
    static UacTheme detect(const QString &override = {});
};

// The dialog card itself (header band, details, password, Yes/No).
class UacCard : public QWidget
{
    Q_OBJECT
public:
    UacCard(const UacInfo &info, const UacTheme &theme, QWidget *parent = nullptr);

    void setPrompt(const QString &prompt, bool echo);
    // Shows the password box (switches out of Yes/No-only mode if needed).
    void requirePassword(const QString &prompt, bool echo);
    bool isConsent() const { return m_consent; }
    void setError(const QString &text);
    void setInfo(const QString &text);
    void setBusy(bool busy);
    void clearPassword();
    void focusInput();
    void reject() { Q_EMIT rejected(); }
    void setDetailsExpanded(bool expanded);

Q_SIGNALS:
    void accepted(const QString &response);
    void rejected();
    void userChanged(int index);
    void geometryHintChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateYes();
    void submit();

    UacInfo m_info;
    UacTheme m_theme;
    QWidget *m_band = nullptr;
    QWidget *m_footer = nullptr;
    QWidget *m_details = nullptr;
    QWidget *m_creds = nullptr;
    QLabel *m_detailsLink = nullptr;
    QLabel *m_avatar = nullptr;
    QLabel *m_userName = nullptr;
    QLabel *m_userSub = nullptr;
    QComboBox *m_userCombo = nullptr;
    QLineEdit *m_password = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_yes = nullptr;
    QPushButton *m_no = nullptr;
    bool m_busy = false;
    bool m_expanded = false;
    bool m_consent = false;
};

// Full-screen dimmed backdrop (one per monitor) that hosts the card,
// similar to the Windows "secure desktop" look.
class UacPrompt : public QObject
{
    Q_OBJECT
public:
    UacPrompt(const UacInfo &info, const UacTheme &theme, UacBackdrop backdrop, QObject *parent = nullptr);
    ~UacPrompt() override;

    UacCard *card() const { return m_card; }
    void show();
    void close();

    // Renders the card over a sample wallpaper into an image (used for screenshots).
    static QImage renderPreview(const UacInfo &info, const UacTheme &theme, const QSize &size);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void recenter();

    QList<QPointer<QWidget>> m_overlays;
    QPointer<QWidget> m_host;
    UacCard *m_card = nullptr;
    bool m_dim = true;
};
