#include "displaysettings.h"
#include <QQuickItem>

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontInfo>
#include <QGuiApplication>
#include <QScreen>
#include <cmath>
#include <QImageReader>
#include <QKeySequence>
#include <QtConcurrentRun>

namespace {
QString displayFile()
{
    // Keep small view preferences separate from the potentially large library cache.
    return QFileInfo(QSettings().fileName()).absoluteDir().filePath(QStringLiteral("display.ini"));
}
}

DisplaySettings::DisplaySettings(QObject *parent) : DisplaySettings(displayFile(), parent) {}
DisplaySettings::DisplaySettings(const QString &file, QObject *parent)
    : QObject(parent), m_settings(file, QSettings::IniFormat)
{
    initialize();
}
DisplaySettings::~DisplaySettings() { if (m_saveTimer.isActive()) save(); }
void DisplaySettings::settleLayout(QQuickItem *item)
{
    QList<QQuickItem *> ancestors;
    for (auto *current = item; current; current = current->parentItem()) ancestors.prepend(current);
    for (auto *ancestor : ancestors) ancestor->ensurePolished();
}

void DisplaySettings::initialize()
{
    loadLibraryViewSettings();
    m_reducedMotion = m_settings.value("view/reducedMotion", false).toBool();
    m_shortcuts = m_settings.value("keyboard/shortcuts").toMap();
    m_theme = m_settings.value("appearance/theme", "dark").toString() == "light" ? "light" : "dark";
    m_artworkColors = m_settings.value("appearance/artworkColors", false).toBool();
    m_seekSeconds = qBound(5, m_settings.value("playback/seekSeconds", 10).toInt(), 30);
    m_splitArtists = m_settings.value("library/splitArtists", false).toBool();
    const auto separators = m_settings.value("library/artistSeparators", ",;|").toString();
    m_artistSeparators.clear();
    for (const auto character : QStringLiteral(",;|"))
        if (separators.contains(character)) m_artistSeparators += character;
    connect(&m_artworkWatcher, &QFutureWatcher<QColor>::finished, this, [this] {
        m_artworkCache.insert(m_readingArtwork.toString(), new QColor(m_artworkWatcher.result()));
        if (m_readingArtwork == m_artworkUrl) { m_artworkAccent = m_artworkWatcher.result(); emit appearanceChanged(); }
        else analyzeArtwork();
    });
    const auto density = m_settings.value("view/density", "automatic").toString();
    if (QStringList{"automatic", "compact", "comfortable"}.contains(density)) m_density = density;
    const auto scale = m_settings.value("view/textScale", 1).toDouble();
    if (std::isfinite(scale)) m_textScale = qBound(1.0, scale, 2.0);
    m_font = QGuiApplication::font();
    m_saveTimer.setSingleShot(true); m_saveTimer.setInterval(250);
    connect(&m_saveTimer, &QTimer::timeout, this, &DisplaySettings::save);
    qGuiApp->installEventFilter(this);
    connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, this, &DisplaySettings::systemFontChanged);
    watchScreen(QGuiApplication::primaryScreen());
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen *screen) {
        watchScreen(screen); emit screenChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) {
        for (const auto &window : m_windows) if (window) fitWindow(window);
        emit screenChanged();
    });
}

QString DisplaySettings::fontFamily() const { return QFontInfo(m_font).family(); }
qreal DisplaySettings::fontPointSize() const
{
    if (m_font.pointSizeF() > 0) return m_font.pointSizeF();
    return m_font.pixelSize() > 0 ? m_font.pixelSize() * 72.0 / logicalDpi() : 10;
}
qreal DisplaySettings::logicalDpi() const
{
    const auto *screen = m_primary ? m_primary->screen() : QGuiApplication::primaryScreen();
    return screen ? screen->logicalDotsPerInch() : 96;
}
QRect DisplaySettings::availableGeometry() const
{
    const auto *screen = m_primary ? m_primary->screen() : QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
}
int DisplaySettings::windowWidth() const { return m_primary ? m_primary->width() : availableGeometry().width(); }
int DisplaySettings::windowHeight() const { return m_primary ? m_primary->height() : availableGeometry().height(); }

void DisplaySettings::updateSystemFont()
{
    const auto font = QGuiApplication::font();
    if (font == m_font) return;
    m_font = font;
    emit systemFontChanged();
}
bool DisplaySettings::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::ThemeChange)
        updateSystemFont();
    return QObject::eventFilter(object, event);
}

void DisplaySettings::setDensity(const QString &density)
{
    if (density == m_density || !QStringList{"automatic", "compact", "comfortable"}.contains(density)) return;
    m_density = density; m_saveTimer.start(); emit preferencesChanged();
}
void DisplaySettings::setTextScale(qreal scale)
{
    if (!std::isfinite(scale)) return;
    scale = qBound(1.0, scale, 2.0);
    if (qFuzzyCompare(scale, m_textScale)) return;
    m_textScale = scale; m_saveTimer.start(); emit preferencesChanged();
}
void DisplaySettings::reset() { setDensity(QStringLiteral("automatic")); setTextScale(1); }
QVariantMap DisplaySettings::exportPreferences()
{
    save();
    QVariantMap values;
    for (const auto &key : m_settings.allKeys()) values.insert(key, m_settings.value(key));
    return values;
}
void DisplaySettings::importPreferences(const QVariantMap &values)
{
    resetSongColumns();
    if (values.contains("libraryView/songColumns")) setSongColumns(values.value("libraryView/songColumns").toList());
    setAvoidRecentShuffle(values.value("playback/avoidRecentShuffle", false).toBool());
    setShuffleRecentDays(values.value("playback/shuffleRecentDays", 7).toInt());
    setDensity(values.value("view/density", "automatic").toString());
    setTextScale(values.value("view/textScale", 1).toDouble());
    setTheme(values.value("appearance/theme", "dark").toString());
    setArtworkColors(values.value("appearance/artworkColors", false).toBool());
    setSeekSeconds(values.value("playback/seekSeconds", 10).toInt());
    setSplitArtists(values.value("library/splitArtists", false).toBool());
    setArtistSeparators(values.value("library/artistSeparators", ",;|").toString());
    setReducedMotion(values.value("view/reducedMotion", false).toBool());
    m_shortcuts = values.value("keyboard/shortcuts").toMap();
    emit shortcutsChanged();
    save();
}
void DisplaySettings::save()
{
    m_saveTimer.stop();
    m_settings.setValue("view/density", m_density);
    m_settings.setValue("view/textScale", m_textScale);
    m_settings.setValue("appearance/theme", m_theme);
    m_settings.setValue("appearance/artworkColors", m_artworkColors);
    m_settings.setValue("playback/seekSeconds", m_seekSeconds);
    m_settings.setValue("library/splitArtists", m_splitArtists);
    m_settings.setValue("library/artistSeparators", m_artistSeparators);
    m_settings.setValue("view/reducedMotion", m_reducedMotion);
    m_settings.setValue("keyboard/shortcuts", m_shortcuts);
    m_settings.setValue("libraryView/songColumns", m_songColumns);
    m_settings.setValue("playback/avoidRecentShuffle", m_avoidRecentShuffle);
    m_settings.setValue("playback/shuffleRecentDays", m_shuffleRecentDays);
    m_settings.sync();
}

void DisplaySettings::setTheme(const QString &theme)
{
    if (theme == m_theme || (theme != "light" && theme != "dark")) return;
    m_theme = theme; m_saveTimer.start(); emit appearanceChanged();
}
void DisplaySettings::setArtworkColors(bool enabled)
{
    if (m_artworkColors == enabled) return;
    m_artworkColors = enabled; m_saveTimer.start(); emit appearanceChanged();
    if (enabled) analyzeArtwork();
}
void DisplaySettings::setSeekSeconds(int seconds)
{
    seconds = qBound(5, seconds, 30);
    if (seconds == m_seekSeconds) return;
    m_seekSeconds = seconds; m_saveTimer.start(); emit seekSecondsChanged();
}
void DisplaySettings::setSplitArtists(bool enabled)
{
    if (enabled == m_splitArtists) return;
    m_splitArtists = enabled; m_saveTimer.start(); emit artistSettingsChanged();
}
void DisplaySettings::setArtistSeparators(const QString &separators)
{
    QString normalized;
    for (const auto character : QStringLiteral(",;|"))
        if (separators.contains(character)) normalized += character;
    if (normalized == m_artistSeparators) return;
    m_artistSeparators = normalized; m_saveTimer.start(); emit artistSettingsChanged();
}
QColor DisplaySettings::accentColor() const
{
    if (!m_artworkColors || !m_artworkAccent.isValid()) return m_theme == "light" ? QColor("#006c67") : QColor("#52d6cd");
    const auto h = m_artworkAccent.hsvHueF();
    const auto s = qBound(0.35, double(m_artworkAccent.hsvSaturationF()), 0.65);
    // Keep the accent readable against the selected light/dark background.
    return QColor::fromHsvF(h < 0 ? 0.48 : h, s, m_theme == "light" ? 0.40 : 0.88);
}
void DisplaySettings::setArtwork(const QUrl &url)
{
    if (url == m_artworkUrl) return;
    m_artworkUrl = url; m_artworkAccent = QColor(); emit appearanceChanged();
    if (m_artworkColors) analyzeArtwork();
}
void DisplaySettings::analyzeArtwork()
{
    if (!m_artworkColors || m_artworkUrl.isEmpty() || m_artworkWatcher.isRunning()) return;
    const auto key = m_artworkUrl.toString();
    if (const auto *color = m_artworkCache.object(key)) { m_artworkAccent = *color; emit appearanceChanged(); return; }
    if (!m_artworkUrl.isLocalFile() && m_artworkUrl.scheme() != "qrc") return;
    const auto path = m_artworkUrl.isLocalFile() ? m_artworkUrl.toLocalFile() : ":" + m_artworkUrl.path();
    m_readingArtwork = m_artworkUrl;
    m_artworkWatcher.setFuture(QtConcurrent::run([path] {
        QImageReader reader(path);
        const auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) return QColor();
        reader.setScaledSize(size.scaled(64, 64, Qt::KeepAspectRatio));
        const auto image = reader.read(); if (image.isNull()) return QColor();
        struct Bucket { double r = 0, g = 0, b = 0, weight = 0; } buckets[24];
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.alphaF() < .5 || color.valueF() < .12 || color.valueF() > .96 || color.hsvSaturationF() < .15) continue;
            const int bin = qBound(0, int(color.hsvHueF() * 24), 23);
            auto &bucket = buckets[bin]; const double weight = color.hsvSaturationF() * color.alphaF();
            bucket.r += color.redF() * weight; bucket.g += color.greenF() * weight; bucket.b += color.blueF() * weight; bucket.weight += weight;
        }
        const auto *best = &buckets[0]; for (const auto &bucket : buckets) if (bucket.weight > best->weight) best = &bucket;
        return best->weight > 0 ? QColor::fromRgbF(best->r / best->weight, best->g / best->weight, best->b / best->weight) : QColor();
    }));
}

void DisplaySettings::watchScreen(QScreen *screen)
{
    if (!screen || m_screens.contains(screen)) return;
    m_screens.insert(screen);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] {
        emit screenChanged(); emit systemFontChanged();
    });
    connect(screen, &QScreen::availableGeometryChanged, this, [this] {
        emit screenChanged();
        for (const auto &window : m_windows) if (window) fitWindow(window);
    });
    connect(screen, &QObject::destroyed, this, [this, screen] { m_screens.remove(screen); });
}

void DisplaySettings::watchWindow(QWindow *window, bool primary)
{
    if (!window || m_windows.contains(window)) return;
    m_windows.append(window);
    if (primary) m_primary = window;
    watchScreen(window->screen());
    connect(window, &QWindow::screenChanged, this, [this, window](QScreen *screen) {
        watchScreen(screen); emit screenChanged(); emit systemFontChanged(); fitWindow(window);
    });
    if (primary) {
        connect(window, &QWindow::widthChanged, this, &DisplaySettings::windowSizeChanged);
        connect(window, &QWindow::heightChanged, this, &DisplaySettings::windowSizeChanged);
    }
    QPointer<QWindow> guarded = window;
    QTimer::singleShot(0, this, [this, guarded] { if (guarded) fitWindow(guarded); });
    emit screenChanged(); emit windowSizeChanged();
}

QRect DisplaySettings::fittedGeometry(const QRect &window, const QRect &available)
{
    if (available.isEmpty()) return window;
    const QSize size = window.size().boundedTo(available.size());
    return QRect(QPoint(qBound(available.left(), window.x(), available.right() - size.width() + 1),
                        qBound(available.top(), window.y(), available.bottom() - size.height() + 1)), size);
}
void DisplaySettings::fitWindow(QWindow *window)
{
    if (!window || !window->screen() || window->windowState() != Qt::WindowNoState) return;
    const auto frame = window->frameMargins();
    const auto available = window->screen()->availableGeometry().marginsRemoved(frame);
    const auto fitted = fittedGeometry(window->geometry(), available);
    if (window->geometry() != fitted) window->setGeometry(fitted);
}
