#include "uacprompt.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QScreen>
#include <QStyleHints>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWindow>
#include <QtMath>

namespace {

constexpr int kCardWidth = 460;
constexpr int kAvatarSize = 40;

QString css(const QColor &c)
{
    return c.alpha() == 255 ? c.name() : c.name(QColor::HexArgb);
}

QColor readableOn(const QColor &bg)
{
    // Relative luminance check: dark text on light accents, white on dark ones.
    const qreal l = 0.2126 * bg.redF() + 0.7152 * bg.greenF() + 0.0722 * bg.blueF();
    return l > 0.55 ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

QPixmap makeAvatar(const UacUser &user, int size, const UacTheme &theme)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, size, size));
    p.setClipPath(circle);

    QImage img;
    if (!user.avatarPath.isEmpty())
        img.load(user.avatarPath);

    if (!img.isNull()) {
        const QImage scaled = img.scaled(QSize(size, size) * dpr, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
        const QRectF src((scaled.width() - size * dpr) / 2.0, (scaled.height() - size * dpr) / 2.0,
                         size * dpr, size * dpr);
        p.drawImage(QRectF(0, 0, size, size), scaled, src);
    } else {
        // Generic person silhouette, like the Windows default account picture.
        const QColor bg = theme.dark ? QColor(0x4a, 0x4a, 0x4a) : QColor(0xd6, 0xd6, 0xd6);
        const QColor fg = theme.dark ? QColor(0xc8, 0xc8, 0xc8) : QColor(0x8a, 0x8a, 0x8a);
        p.fillRect(QRectF(0, 0, size, size), bg);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        const qreal s = size;
        p.drawEllipse(QPointF(s * 0.5, s * 0.38), s * 0.17, s * 0.17);
        p.drawEllipse(QRectF(s * 0.18, s * 0.62, s * 0.64, s * 0.62));
    }
    return pm;
}

QLabel *plainLabel(const QString &text, const char *objectName, QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setTextFormat(Qt::PlainText); // never interpret file names as HTML
    l->setText(text);
    l->setObjectName(QLatin1String(objectName));
    l->setWordWrap(true);
    return l;
}

// Full-screen dim layer. One per monitor.
class DimOverlay : public QWidget
{
    Q_OBJECT
public:
    DimOverlay()
        : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setWindowTitle(QStringLiteral("User Account Control"));
        m_fade = new QVariantAnimation(this);
        m_fade->setStartValue(0.0);
        m_fade->setEndValue(1.0);
        m_fade->setDuration(200);
        m_fade->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_level = v.toReal();
            update();
        });
    }

Q_SIGNALS:
    void clickedOutside();

protected:
    void showEvent(QShowEvent *e) override
    {
        QWidget::showEvent(e);
        m_fade->start();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), QColor(0, 0, 0, qRound(150 * m_level)));
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        e->accept();
        Q_EMIT clickedOutside();
    }

private:
    QVariantAnimation *m_fade = nullptr;
    qreal m_level = 0.0;
};

} // namespace

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

UacTheme UacTheme::detect(const QString &override)
{
    UacTheme t;
    const QPalette pal = qApp->palette();

    if (override == QLatin1String("dark")) {
        t.dark = true;
    } else if (override == QLatin1String("light")) {
        t.dark = false;
    } else {
        t.dark = pal.color(QPalette::Window).lightness() < 128;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        const Qt::ColorScheme cs = QGuiApplication::styleHints()->colorScheme();
        if (cs != Qt::ColorScheme::Unknown)
            t.dark = cs == Qt::ColorScheme::Dark;
#endif
    }

    // Header bands: amber for unknown publishers, blue for packaged software.
    t.bandUnknown = QColor(0x5f, 0x4c, 0x1f);
    t.bandVerified = QColor(0x1f, 0x4a, 0x75);

    // Use the KDE accent (highlight) colour, like Windows uses its accent.
    t.accent = pal.color(QPalette::Highlight);
    if (t.dark && t.accent.lightness() < 120)
        t.accent = t.accent.lighter(150);
    t.accentText = readableOn(t.accent);

    if (t.dark) {
        t.body = QColor(0x2b, 0x2b, 0x2b);
        t.footer = QColor(0x20, 0x20, 0x20);
        t.border = QColor(0x48, 0x48, 0x48);
        t.text = QColor(0xff, 0xff, 0xff);
        t.subtext = QColor(0xcf, 0xcf, 0xcf);
        t.inputBg = QColor(0x1f, 0x1f, 0x1f);
        t.inputBorder = QColor(0x50, 0x50, 0x50);
        t.neutralBtn = QColor(0x37, 0x37, 0x37);
        t.neutralBtnText = QColor(0xff, 0xff, 0xff);
        t.neutralBtnBorder = QColor(0x45, 0x45, 0x45);
        t.error = QColor(0xff, 0x99, 0xa4);
        t.link = t.accent.lightness() < 150 ? t.accent.lighter(140) : t.accent;
    } else {
        t.body = QColor(0xf9, 0xf9, 0xf9);
        t.footer = QColor(0xee, 0xee, 0xee);
        t.border = QColor(0xc8, 0xc8, 0xc8);
        t.text = QColor(0x1b, 0x1b, 0x1b);
        t.subtext = QColor(0x5a, 0x5a, 0x5a);
        t.inputBg = QColor(0xff, 0xff, 0xff);
        t.inputBorder = QColor(0xc2, 0xc2, 0xc2);
        t.neutralBtn = QColor(0xfd, 0xfd, 0xfd);
        t.neutralBtnText = QColor(0x1b, 0x1b, 0x1b);
        t.neutralBtnBorder = QColor(0xc8, 0xc8, 0xc8);
        t.error = QColor(0xc4, 0x2b, 0x1c);
        t.link = t.accent.lightness() > 140 ? t.accent.darker(150) : t.accent;
    }
    return t;
}

// ---------------------------------------------------------------------------
// Card
// ---------------------------------------------------------------------------

UacCard::UacCard(const UacInfo &info, const UacTheme &theme, QWidget *parent)
    : QWidget(parent)
    , m_info(info)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("uacCard"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kCardWidth);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- Coloured header band ------------------------------------------------
    m_band = new QWidget(this);
    m_band->setObjectName(QStringLiteral("band"));
    auto *band = new QVBoxLayout(m_band);
    band->setContentsMargins(24, 8, 8, 22);
    band->setSpacing(8);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto *title = plainLabel(QStringLiteral("User Account Control"), "title", m_band);
    title->setWordWrap(false);
    auto *close = new QToolButton(m_band);
    close->setObjectName(QStringLiteral("close"));
    close->setText(QStringLiteral("✕"));
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedSize(36, 28);
    close->setFocusPolicy(Qt::NoFocus);
    connect(close, &QToolButton::clicked, this, &UacCard::rejected);
    titleRow->addWidget(title, 1, Qt::AlignVCenter);
    titleRow->addWidget(close, 0, Qt::AlignTop);
    band->addLayout(titleRow);

    const QString question = info.verified
        ? QStringLiteral("Do you want to allow this app to make changes to your device?")
        : QStringLiteral("Do you want to allow this app from an unknown publisher to make changes to your device?");
    auto *q = plainLabel(question, "question", m_band);
    q->setContentsMargins(0, 0, 16, 0);
    band->addWidget(q);
    root->addWidget(m_band);

    // --- Body -------------------------------------------------------------------
    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("body"));
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(24, 20, 24, 18);
    bl->setSpacing(4);

    bl->addWidget(plainLabel(info.appName, "appName", body));
    bl->addSpacing(14);
    bl->addWidget(plainLabel((info.verified ? QStringLiteral("Verified publisher: ") : QStringLiteral("Publisher: "))
                                 + info.publisher,
                             "detail", body));
    bl->addWidget(plainLabel(QStringLiteral("File origin: ") + info.origin, "detail", body));

    m_details = new QWidget(body);
    auto *dl = new QVBoxLayout(m_details);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->setSpacing(4);
    auto addDetail = [&](const QString &text) {
        auto *l = plainLabel(text, "detail", m_details);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        dl->addWidget(l);
    };
    addDetail(QStringLiteral("Program location: \"%1\"").arg(info.location));
    if (!info.package.isEmpty())
        addDetail(QStringLiteral("Package: %1").arg(info.package));
    if (!info.interpreter.isEmpty())
        addDetail(QStringLiteral("Run by: %1").arg(info.interpreter));
    if (!info.commandLine.isEmpty())
        addDetail(QStringLiteral("Command line: %1").arg(info.commandLine));
    if (!info.message.isEmpty())
        addDetail(QStringLiteral("Reason: %1").arg(info.message));
    addDetail(QStringLiteral("Action: %1").arg(info.actionId));
    m_details->setVisible(false);
    bl->addWidget(m_details);

    bl->addSpacing(14);
    m_detailsLink = new QLabel(body);
    m_detailsLink->setObjectName(QStringLiteral("link"));
    m_detailsLink->setTextFormat(Qt::RichText);
    m_detailsLink->setCursor(Qt::PointingHandCursor);
    connect(m_detailsLink, &QLabel::linkActivated, this, [this] { setDetailsExpanded(!m_expanded); });
    bl->addWidget(m_detailsLink, 0, Qt::AlignLeft);

    bl->addSpacing(18);
    bl->addWidget(plainLabel(info.users.size() > 1
                                 ? QStringLiteral("To continue, choose an admin account and enter its password.")
                                 : QStringLiteral("To continue, enter an admin password."),
                             "detail", body));
    bl->addSpacing(12);

    // User tile
    auto *tile = new QHBoxLayout;
    tile->setSpacing(12);
    m_avatar = new QLabel(body);
    m_avatar->setFixedSize(kAvatarSize, kAvatarSize);
    tile->addWidget(m_avatar, 0, Qt::AlignVCenter);

    auto *names = new QVBoxLayout;
    names->setSpacing(0);
    if (info.users.size() > 1) {
        m_userCombo = new QComboBox(body);
        m_userCombo->setObjectName(QStringLiteral("users"));
        for (const UacUser &u : info.users)
            m_userCombo->addItem(u.fullName.isEmpty() ? u.name : QStringLiteral("%1 (%2)").arg(u.fullName, u.name));
        m_userCombo->setCurrentIndex(info.selectedUser);
        connect(m_userCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
            if (i >= 0 && i < m_info.users.size()) {
                m_avatar->setPixmap(makeAvatar(m_info.users.at(i), kAvatarSize, m_theme));
                clearPassword();
                Q_EMIT userChanged(i);
            }
        });
        names->addWidget(m_userCombo);
    } else {
        m_userName = plainLabel({}, "userName", body);
        m_userSub = plainLabel({}, "userSub", body);
        names->addWidget(m_userName);
        names->addWidget(m_userSub);
    }
    tile->addLayout(names, 1);
    bl->addLayout(tile);

    if (!info.users.isEmpty()) {
        const UacUser &u = info.users.at(qBound(0, info.selectedUser, info.users.size() - 1));
        m_avatar->setPixmap(makeAvatar(u, kAvatarSize, theme));
        if (m_userName) {
            m_userName->setText(u.fullName.isEmpty() ? u.name : u.fullName);
            m_userSub->setText(u.fullName.isEmpty() ? QStringLiteral("Administrator")
                                                    : QStringLiteral("%1 · Administrator").arg(u.name));
        }
    } else {
        m_avatar->setPixmap(makeAvatar(UacUser{}, kAvatarSize, theme));
        if (m_userName)
            m_userName->setText(QStringLiteral("Administrator"));
    }

    bl->addSpacing(12);
    m_password = new QLineEdit(body);
    m_password->setObjectName(QStringLiteral("password"));
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QStringLiteral("Password"));
    m_password->setMinimumHeight(34);
    m_password->setEnabled(false);
    connect(m_password, &QLineEdit::textChanged, this, &UacCard::updateYes);
    connect(m_password, &QLineEdit::returnPressed, this, &UacCard::submit);
    bl->addWidget(m_password);

    m_message = plainLabel({}, "message", body);
    m_message->setVisible(false);
    bl->addSpacing(4);
    bl->addWidget(m_message);
    root->addWidget(body);

    // --- Footer with Yes / No ---------------------------------------------------
    m_footer = new QWidget(this);
    m_footer->setObjectName(QStringLiteral("footer"));
    auto *fl = new QHBoxLayout(m_footer);
    fl->setContentsMargins(24, 22, 24, 22);
    fl->setSpacing(8);
    m_yes = new QPushButton(QStringLiteral("Yes"), m_footer);
    m_yes->setObjectName(QStringLiteral("yes"));
    m_no = new QPushButton(QStringLiteral("No"), m_footer);
    m_no->setObjectName(QStringLiteral("no"));
    for (QPushButton *b : {m_yes, m_no}) {
        b->setMinimumHeight(32);
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoDefault(false);
        fl->addWidget(b, 1);
    }
    connect(m_yes, &QPushButton::clicked, this, &UacCard::submit);
    connect(m_no, &QPushButton::clicked, this, &UacCard::rejected);
    root->addWidget(m_footer);

    setTabOrder(m_password, m_yes);
    setTabOrder(m_yes, m_no);

    // --- Style --------------------------------------------------------------------
    setStyleSheet(QStringLiteral(R"(
        QLabel { color: %1; font-size: 13px; background: transparent; }
        QLabel#title { color: #ffffff; font-size: 12px; }
        QLabel#question { color: #ffffff; font-size: 20px; }
        QLabel#appName { font-size: 16px; }
        QLabel#detail { color: %2; }
        QLabel#userName { font-size: 14px; }
        QLabel#userSub { color: %2; font-size: 12px; }
        QToolButton#close { color: #ffffff; background: transparent; border: none; border-radius: 4px; font-size: 13px; }
        QToolButton#close:hover { background: #c42b1c; }
        QLineEdit#password {
            background: %3; color: %1; border: 1px solid %4; border-bottom: 2px solid %4;
            border-radius: 4px; padding: 0 10px; font-size: 14px;
            selection-background-color: %5; selection-color: %6;
        }
        QLineEdit#password:focus { border-bottom: 2px solid %5; }
        QLineEdit#password:disabled { color: %2; }
        QComboBox#users {
            background: %3; color: %1; border: 1px solid %4; border-radius: 4px;
            padding: 4px 10px; font-size: 14px; min-height: 26px;
        }
        QComboBox#users QAbstractItemView { background: %3; color: %1; selection-background-color: %5; selection-color: %6; }
        QPushButton { font-size: 13px; border-radius: 4px; padding: 0 12px; }
        QPushButton#yes { background: %5; color: %6; border: 1px solid %5; }
        QPushButton#yes:hover { background: %7; border-color: %7; }
        QPushButton#yes:disabled { background: %8; color: %2; border-color: %9; }
        QPushButton#no { background: %8; color: %10; border: 1px solid %9; }
        QPushButton#no:hover { background: %11; }
        QPushButton:focus { outline: none; border: 2px solid %1; }
    )")
                      .arg(css(theme.text), css(theme.subtext), css(theme.inputBg), css(theme.inputBorder),
                           css(theme.accent), css(theme.accentText), css(theme.accent.lighter(112)),
                           css(theme.neutralBtn), css(theme.neutralBtnBorder))
                      .arg(css(theme.neutralBtnText), css(theme.dark ? theme.neutralBtn.lighter(125)
                                                                       : theme.neutralBtn.darker(106))));

    m_shake = new QVariantAnimation(this);
    m_shake->setStartValue(0.0);
    m_shake->setEndValue(1.0);
    m_shake->setDuration(420);

    setDetailsExpanded(false);
    updateYes();
}

void UacCard::setDetailsExpanded(bool expanded)
{
    m_expanded = expanded;
    m_details->setVisible(expanded);
    m_detailsLink->setText(QStringLiteral("<a href=\"#\" style=\"color:%1; text-decoration:none;\">%2</a>")
                               .arg(css(m_theme.link),
                                    expanded ? QStringLiteral("Hide details") : QStringLiteral("Show more details")));
    if (layout())
        layout()->activate();
    adjustSize();
    Q_EMIT geometryHintChanged();
}

void UacCard::setPrompt(const QString &prompt, bool echo)
{
    QString hint = prompt.trimmed();
    if (hint.endsWith(QLatin1Char(':')))
        hint.chop(1);
    if (hint.isEmpty() || hint.compare(QLatin1String("password"), Qt::CaseInsensitive) == 0)
        hint = QStringLiteral("Password");
    m_password->setPlaceholderText(hint);
    m_password->setEchoMode(echo ? QLineEdit::Normal : QLineEdit::Password);
    setBusy(false);
    focusInput();
}

void UacCard::setError(const QString &text)
{
    m_message->setStyleSheet(QStringLiteral("color: %1;").arg(css(m_theme.error)));
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
    layout()->activate();
    adjustSize();
    Q_EMIT geometryHintChanged();
}

void UacCard::setInfo(const QString &text)
{
    m_message->setStyleSheet(QStringLiteral("color: %1;").arg(css(m_theme.subtext)));
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
    layout()->activate();
    adjustSize();
    Q_EMIT geometryHintChanged();
}

void UacCard::setBusy(bool busy)
{
    m_busy = busy;
    m_password->setEnabled(!busy);
    if (m_userCombo)
        m_userCombo->setEnabled(!busy);
    updateYes();
}

void UacCard::clearPassword()
{
    m_password->clear();
}

void UacCard::focusInput()
{
    if (m_password->isEnabled()) {
        m_password->setFocus(Qt::OtherFocusReason);
    } else {
        m_no->setFocus(Qt::OtherFocusReason);
    }
}

void UacCard::shake()
{
    if (!parentWidget() || m_shake->state() == QAbstractAnimation::Running)
        return;
    const QPoint base = pos();
    m_shake->disconnect(this);
    connect(m_shake, &QVariantAnimation::valueChanged, this, [this, base](const QVariant &v) {
        const qreal t = v.toReal();
        move(base + QPoint(qRound(qSin(t * M_PI * 6) * (1.0 - t) * 12), 0));
    });
    connect(m_shake, &QVariantAnimation::finished, this, [this, base] { move(base); });
    m_shake->start();
    QApplication::beep();
}

void UacCard::updateYes()
{
    m_yes->setEnabled(!m_busy && m_password->isEnabled() && !m_password->text().isEmpty());
}

void UacCard::submit()
{
    if (!m_yes->isEnabled())
        return;
    const QString response = m_password->text();
    setBusy(true);
    setInfo(QStringLiteral("Checking…"));
    Q_EMIT accepted(response);
}

void UacCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, 8, 8);

    p.save();
    p.setClipPath(path);
    p.fillRect(rect(), m_theme.body);
    p.fillRect(m_band->geometry(), m_info.verified ? m_theme.bandVerified : m_theme.bandUnknown);
    p.fillRect(m_footer->geometry(), m_theme.footer);
    p.restore();

    p.setPen(QPen(m_theme.border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void UacCard::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        Q_EMIT rejected();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// Prompt (overlays + card)
// ---------------------------------------------------------------------------

UacPrompt::UacPrompt(const UacInfo &info, const UacTheme &theme, bool dim, QObject *parent)
    : QObject(parent)
    , m_dim(dim)
{
    if (dim) {
        QScreen *hostScreen = QGuiApplication::screenAt(QCursor::pos());
        if (!hostScreen)
            hostScreen = QGuiApplication::primaryScreen();

        const auto screens = QGuiApplication::screens();
        for (QScreen *screen : screens) {
            auto *overlay = new DimOverlay;
            overlay->setGeometry(screen->geometry());
            overlay->setProperty("uacScreen", QVariant::fromValue<void *>(screen));
            m_overlays << overlay;
            if (screen == hostScreen || !m_host)
                m_host = overlay;
        }
        m_card = new UacCard(info, theme, m_host);
        m_host->installEventFilter(this);
        connect(m_card, &UacCard::geometryHintChanged, this, &UacPrompt::recenter);
        for (const auto &o : qAsConst(m_overlays)) {
            connect(static_cast<DimOverlay *>(o.data()), &DimOverlay::clickedOutside, this, [this] {
                if (m_host) {
                    m_host->raise();
                    m_host->activateWindow();
                }
                m_card->shake();
                m_card->focusInput();
            });
        }
    } else {
        m_card = new UacCard(info, theme, nullptr);
        m_card->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        m_card->setAttribute(Qt::WA_TranslucentBackground);
        m_card->setWindowTitle(QStringLiteral("User Account Control"));
    }
}

UacPrompt::~UacPrompt()
{
    close();
}

void UacPrompt::show()
{
    if (m_dim) {
        for (const auto &o : qAsConst(m_overlays)) {
            if (!o)
                continue;
            o->winId(); // create the native window so we can pick its screen
            if (auto *screen = static_cast<QScreen *>(o->property("uacScreen").value<void *>()))
                o->windowHandle()->setScreen(screen);
            o->showFullScreen();
        }
        m_card->adjustSize();
        recenter();
        m_card->show();
        if (m_host) {
            m_host->raise();
            m_host->activateWindow();
        }
    } else {
        m_card->adjustSize();
        if (QScreen *s = QGuiApplication::primaryScreen())
            m_card->move(s->geometry().center() - m_card->rect().center());
        m_card->show();
        m_card->raise();
        m_card->activateWindow();
    }
    m_card->focusInput();
}

void UacPrompt::close()
{
    for (const auto &o : qAsConst(m_overlays)) {
        if (o) {
            o->hide();
            o->deleteLater();
        }
    }
    m_overlays.clear();
    if (!m_dim && m_card) {
        m_card->hide();
        m_card->deleteLater();
    }
    m_card = nullptr;
}

bool UacPrompt::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host && event->type() == QEvent::Resize)
        recenter();
    return QObject::eventFilter(watched, event);
}

void UacPrompt::recenter()
{
    if (!m_dim || !m_host || !m_card)
        return;
    const QPoint c = m_host->rect().center() - m_card->rect().center();
    m_card->move(qMax(0, c.x()), qMax(0, c.y() - 20));
}

QImage UacPrompt::renderPreview(const UacInfo &info, const UacTheme &theme, const QSize &size)
{
    // Sample "wallpaper" so the screenshot looks like a real desktop.
    QPixmap wallpaper(size);
    {
        QPainter p(&wallpaper);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient bg(0, 0, size.width(), size.height());
        bg.setColorAt(0, QColor(0x0b, 0x1a, 0x3a));
        bg.setColorAt(1, QColor(0x14, 0x0f, 0x2e));
        p.fillRect(wallpaper.rect(), bg);
        QRadialGradient glow(size.width() * 0.3, size.height() * 0.75, size.width() * 0.6);
        glow.setColorAt(0, QColor(0x3d, 0xae, 0xe9, 170));
        glow.setColorAt(1, QColor(0x3d, 0xae, 0xe9, 0));
        p.fillRect(wallpaper.rect(), glow);
        QRadialGradient glow2(size.width() * 0.8, size.height() * 0.2, size.width() * 0.45);
        glow2.setColorAt(0, QColor(0x8e, 0x44, 0xad, 140));
        glow2.setColorAt(1, QColor(0x8e, 0x44, 0xad, 0));
        p.fillRect(wallpaper.rect(), glow2);
        p.fillRect(wallpaper.rect(), QColor(0, 0, 0, 150)); // the dim layer
    }

    auto *scene = new QLabel;
    scene->setPixmap(wallpaper);
    scene->resize(size);
    auto *card = new UacCard(info, theme, scene);
    card->setPrompt(QStringLiteral("Password:"), false);
    scene->show();
    card->layout()->activate();
    card->adjustSize();
    card->move((size.width() - card->width()) / 2, (size.height() - card->height()) / 2);
    card->show();
    QCoreApplication::processEvents();
    const QImage img = scene->grab().toImage();
    delete scene;
    return img;
}

#include "uacprompt.moc"
