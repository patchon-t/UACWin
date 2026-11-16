#include "wallpaper.h"
#include "ini.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace {

QString configHome()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

bool isImageFile(const QString &path)
{
    static const QStringList exts{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                  QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("avif"),
                                  QStringLiteral("jxl")};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

QStringList imagesIn(const QString &dir)
{
    QStringList out;
    const auto entries = QDir(dir).entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &fi : entries) {
        if (isImageFile(fi.filePath()))
            out << fi.filePath();
    }
    return out;
}

// Wallpaper packages ship contents/images/1920x1080.png etc. Pick the smallest
// one that still covers the screen, otherwise the biggest.
QString bestFromPackage(const QString &dir, bool dark, const QSize &target)
{
    QStringList files;
    if (dark)
        files = imagesIn(dir + QStringLiteral("/contents/images_dark"));
    if (files.isEmpty())
        files = imagesIn(dir + QStringLiteral("/contents/images"));
    if (files.isEmpty())
        files = imagesIn(dir);
    if (files.isEmpty())
        return {};

    static const QRegularExpression re(QStringLiteral("(\\d+)x(\\d+)"));
    QString best, biggest;
    qint64 bestArea = 0, biggestArea = -1;
    for (const QString &f : files) {
        const auto m = re.match(QFileInfo(f).completeBaseName());
        const int w = m.hasMatch() ? m.captured(1).toInt() : 0;
        const int h = m.hasMatch() ? m.captured(2).toInt() : 0;
        const qint64 area = qint64(w) * h;
        if (area > biggestArea) {
            biggestArea = area;
            biggest = f;
        }
        if (w >= target.width() && h >= target.height() && (best.isEmpty() || area < bestArea)) {
            best = f;
            bestArea = area;
        }
    }
    return best.isEmpty() ? biggest : best;
}

QString resolveImage(QString value, bool dark, const QSize &target)
{
    value = value.trimmed();
    if (value.isEmpty())
        return {};
    if (value.startsWith(QLatin1String("file:")))
        value = QUrl(value).toLocalFile();
    const QFileInfo fi(value);
    if (fi.isDir())
        return bestFromPackage(value, dark, target);
    if (fi.isFile())
        return value;
    return {};
}

QString defaultWallpaper(bool dark, const QSize &target)
{
    const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &d : dirs) {
        const QString f = resolveImage(d + QStringLiteral("/wallpapers/Next"), dark, target);
        if (!f.isEmpty())
            return f;
    }
    return {};
}

QString fromContainment(const IniSections &ini, const QString &key, bool dark, const QSize &target)
{
    const QString plugin = ini.value(key).value(QStringLiteral("wallpaperplugin"), QStringLiteral("org.kde.image"));
    if (plugin == QLatin1String("org.kde.image")) {
        const QString image = ini.value(key + QStringLiteral("][Wallpaper][org.kde.image][General"))
                                  .value(QStringLiteral("Image"));
        return image.isEmpty() ? defaultWallpaper(dark, target) : resolveImage(image, dark, target);
    }
    if (plugin == QLatin1String("org.kde.slideshow")) {
        const auto general = ini.value(key + QStringLiteral("][Wallpaper][org.kde.slideshow][General"));
        // Use the picture currently shown if Plasma remembers it, else the first one.
        QString current = general.value(QStringLiteral("Image"));
        QString f = resolveImage(current, dark, target);
        if (!f.isEmpty())
            return f;
        const QStringList paths = general.value(QStringLiteral("SlidePaths")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &p : paths) {
            QString dir = p.trimmed();
            if (dir.startsWith(QLatin1String("file:")))
                dir = QUrl(dir).toLocalFile();
            const QStringList imgs = imagesIn(dir);
            if (!imgs.isEmpty())
                return imgs.first();
            const auto subdirs = QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &sub : subdirs) {
                f = bestFromPackage(sub.filePath(), dark, target);
                if (!f.isEmpty())
                    return f;
            }
        }
    }
    return {}; // plain colour or unknown plugin
}

void boxBlurPass(QImage &img, int radius, bool horizontal)
{
    const int w = img.width(), h = img.height();
    const int outer = horizontal ? h : w;
    const int inner = horizontal ? w : h;
    QVector<QRgb> line(inner);
    for (int o = 0; o < outer; ++o) {
        auto px = [&](int i) -> QRgb & {
            return horizontal ? reinterpret_cast<QRgb *>(img.scanLine(o))[i]
                              : reinterpret_cast<QRgb *>(img.scanLine(i))[o];
        };
        for (int i = 0; i < inner; ++i)
            line[i] = px(i);
        int r = 0, g = 0, b = 0;
        const int n = 2 * radius + 1;
        for (int k = -radius; k <= radius; ++k) {
            const QRgb c = line[qBound(0, k, inner - 1)];
            r += qRed(c);
            g += qGreen(c);
            b += qBlue(c);
        }
        for (int i = 0; i < inner; ++i) {
            px(i) = qRgb(r / n, g / n, b / n);
            const QRgb add = line[qMin(i + radius + 1, inner - 1)];
            const QRgb sub = line[qMax(i - radius, 0)];
            r += qRed(add) - qRed(sub);
            g += qGreen(add) - qGreen(sub);
            b += qBlue(add) - qBlue(sub);
        }
    }
}

} // namespace

QString UacWallpaper::desktopWallpaper(int plasmaScreen, bool dark, const QSize &targetPx)
{
    const QString override = qEnvironmentVariable("UACWIN_WALLPAPER");
    if (!override.isEmpty()) {
        const QString f = resolveImage(override, dark, targetPx);
        if (!f.isEmpty())
            return f;
    }

    const IniSections ini = readIni(configHome() + QStringLiteral("/plasma-org.kde.plasma.desktop-appletsrc"));
    const QString activity = readIni(configHome() + QStringLiteral("/kactivitymanagerdrc"))
                                 .value(QStringLiteral("main")).value(QStringLiteral("currentActivity"));

    // Desktop containments are "[Containments][N]" with no panel location.
    static const QRegularExpression containmentRe(QStringLiteral("^Containments\\]\\[(\\d+)$"));
    QString bestKey;
    int bestScore = -1;
    for (auto it = ini.constBegin(); it != ini.constEnd(); ++it) {
        if (!containmentRe.match(it.key()).hasMatch())
            continue;
        const auto &c = it.value();
        if (c.value(QStringLiteral("location"), QStringLiteral("0")) != QLatin1String("0")
            || c.value(QStringLiteral("formfactor"), QStringLiteral("0")) != QLatin1String("0"))
            continue;
        if (!c.contains(QStringLiteral("wallpaperplugin")) && !c.contains(QStringLiteral("lastScreen")))
            continue;
        const int screen = c.value(QStringLiteral("lastScreen"), QStringLiteral("-1")).toInt();
        const QString act = c.value(QStringLiteral("activityId"));
        int score = 0;
        if (screen == plasmaScreen)
            score += 4;
        else if (screen == 0)
            score += 1;
        if (!activity.isEmpty() && act == activity)
            score += 2;
        if (score > bestScore) {
            bestScore = score;
            bestKey = it.key();
        }
    }

    QString file;
    if (!bestKey.isEmpty())
        file = fromContainment(ini, bestKey, dark, targetPx);

    if (file.isEmpty()) {
        // Lock screen wallpaper is the next best guess.
        file = resolveImage(readIni(configHome() + QStringLiteral("/kscreenlockerrc"))
                                .value(QStringLiteral("Greeter][Wallpaper][org.kde.image][General"))
                                .value(QStringLiteral("Image")),
                            dark, targetPx);
    }
    if (file.isEmpty() && bestKey.isEmpty())
        file = defaultWallpaper(dark, targetPx);
    return file;
}

QImage UacWallpaper::blur(const QImage &sharp, const QSize &targetPx)
{
    if (sharp.isNull() || targetPx.isEmpty())
        return {};

    // Blur a small copy and scale it back up: fast and very smooth.
    const int sw = qMax(32, targetPx.width() / 24);
    const int sh = qMax(18, targetPx.height() * sw / targetPx.width());
    QImage small = sharp.scaled(QSize(sw, sh), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    small = small.copy((small.width() - sw) / 2, (small.height() - sh) / 2, sw, sh)
                .convertToFormat(QImage::Format_RGB32);
    for (int i = 0; i < 3; ++i) {
        boxBlurPass(small, 4, true);
        boxBlurPass(small, 4, false);
    }

    // Upscale in two bilinear steps; one huge step leaves faint blocky seams.
    QImage mid = small.scaled(small.size() * 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    for (int i = 0; i < 2; ++i) {
        boxBlurPass(mid, 3, true);
        boxBlurPass(mid, 3, false);
    }
    QImage out = mid.scaled(targetPx, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_RGB32);
    QPainter p(&out);
    p.fillRect(out.rect(), QColor(0, 0, 0, 70)); // a little darker so the prompt stands out
    return out;
}

QImage UacWallpaper::blurredFile(const QString &path, const QSize &targetPx)
{
    static QHash<QString, QImage> cache;
    const QFileInfo fi(path);
    const QString key = QStringLiteral("%1|%2|%3x%4")
                            .arg(fi.canonicalFilePath())
                            .arg(fi.lastModified().toMSecsSinceEpoch())
                            .arg(targetPx.width())
                            .arg(targetPx.height());
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QImageReader reader(path);
    reader.setAutoTransform(true);
    // Decode at reduced size when the format supports it (JPEG does): we blur anyway.
    const QSize full = reader.size();
    if (full.isValid() && full.width() > targetPx.width() / 4 && reader.supportsOption(QImageIOHandler::ScaledSize)) {
        const qreal f = qMax(qreal(targetPx.width()) / 8 / full.width(), qreal(targetPx.height()) / 8 / full.height());
        if (f < 1.0)
            reader.setScaledSize(full * f);
    }
    const QImage img = blur(reader.read(), targetPx);
    if (cache.size() > 8)
        cache.clear();
    if (!img.isNull())
        cache.insert(key, img);
    return img;
}
