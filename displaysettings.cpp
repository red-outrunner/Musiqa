#include "displaysettings.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontInfo>
#include <QGuiApplication>
#include <QScreen>
#include <cmath>

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

void DisplaySettings::initialize()
{
    const auto density = m_settings.value("view/density", "automatic").toString();
    if (QStringList{"automatic", "compact", "comfortable"}.contains(density)) m_density = density;
    const auto scale = m_settings.value("view/textScale", 1).toDouble();
    if (std::isfinite(scale)) m_textScale = qBound(1.0, scale, 2.0);
    m_font = QGuiApplication::font();
    m_saveTimer.setSingleShot(true); m_saveTimer.setInterval(250);
    connect(&m_saveTimer, &QTimer::timeout, this, &DisplaySettings::save);
    qGuiApp->installEventFilter(this);
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
void DisplaySettings::save()
{
    m_saveTimer.stop();
    m_settings.setValue("view/density", m_density);
    m_settings.setValue("view/textScale", m_textScale);
    m_settings.sync();
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
