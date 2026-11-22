#pragma once

#include <QFont>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QWindow>
#include <QColor>
#include <QUrl>
#include <QFutureWatcher>
#include <QCache>
#include <QQuickItem>

class DisplaySettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY systemFontChanged)
    Q_PROPERTY(qreal fontPointSize READ fontPointSize NOTIFY systemFontChanged)
    Q_PROPERTY(qreal logicalDpi READ logicalDpi NOTIFY screenChanged)
    Q_PROPERTY(QRect availableGeometry READ availableGeometry NOTIFY screenChanged)
    Q_PROPERTY(int windowWidth READ windowWidth NOTIFY windowSizeChanged)
    Q_PROPERTY(int windowHeight READ windowHeight NOTIFY windowSizeChanged)
    Q_PROPERTY(QString density READ density WRITE setDensity NOTIFY preferencesChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY preferencesChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY appearanceChanged)
    Q_PROPERTY(bool artworkColors READ artworkColors WRITE setArtworkColors NOTIFY appearanceChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY appearanceChanged)
    Q_PROPERTY(int seekSeconds READ seekSeconds WRITE setSeekSeconds NOTIFY seekSecondsChanged)
    Q_PROPERTY(bool splitArtists READ splitArtists WRITE setSplitArtists NOTIFY artistSettingsChanged)
    Q_PROPERTY(QString artistSeparators READ artistSeparators WRITE setArtistSeparators NOTIFY artistSettingsChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY motionChanged)
    Q_PROPERTY(bool reduceEffects READ reduceEffects NOTIFY motionChanged)
    Q_PROPERTY(QString batterySaverMode READ batterySaverMode WRITE setBatterySaverMode NOTIFY desktopPreferencesChanged)
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY powerSourceChanged)
    Q_PROPERTY(bool batterySaverActive READ batterySaverActive NOTIFY powerSourceChanged)
    Q_PROPERTY(bool resumeAfterSleep READ resumeAfterSleep WRITE setResumeAfterSleep NOTIFY desktopPreferencesChanged)
    Q_PROPERTY(bool pauseOnDisconnect READ pauseOnDisconnect WRITE setPauseOnDisconnect NOTIFY desktopPreferencesChanged)
    Q_PROPERTY(bool highContrast READ highContrast WRITE setHighContrast NOTIFY appearanceChanged)
    Q_PROPERTY(bool largeControls READ largeControls WRITE setLargeControls NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList shortcutOptions READ shortcutOptions NOTIFY shortcutsChanged)
    Q_PROPERTY(bool capturingShortcut READ capturingShortcut WRITE setCapturingShortcut NOTIFY shortcutCaptureChanged)
    Q_PROPERTY(QVariantList songColumns READ songColumns WRITE setSongColumns NOTIFY libraryViewChanged)
    Q_PROPERTY(bool columnsCustomized READ columnsCustomized NOTIFY libraryViewChanged)
    Q_PROPERTY(bool avoidRecentShuffle READ avoidRecentShuffle WRITE setAvoidRecentShuffle NOTIFY libraryViewChanged)
    Q_PROPERTY(int shuffleRecentDays READ shuffleRecentDays WRITE setShuffleRecentDays NOTIFY libraryViewChanged)
public:
    explicit DisplaySettings(QObject *parent = nullptr);
    DisplaySettings(const QString &settingsFile, QObject *parent = nullptr);
    ~DisplaySettings() override;
    QString fontFamily() const;
    qreal fontPointSize() const;
    qreal logicalDpi() const;
    QRect availableGeometry() const;
    int windowWidth() const;
    int windowHeight() const;
    QString density() const { return m_density; }
    qreal textScale() const { return m_textScale; }
    void setDensity(const QString &density);
    void setTextScale(qreal scale);
    QString theme() const { return m_theme; }
    bool artworkColors() const { return m_artworkColors; }
    QColor accentColor() const;
    int seekSeconds() const { return m_seekSeconds; }
    void setTheme(const QString &theme);
    void setArtworkColors(bool enabled);
    void setSeekSeconds(int seconds);
    bool splitArtists() const { return m_splitArtists; }
    QString artistSeparators() const { return m_artistSeparators; }
    void setSplitArtists(bool enabled);
    void setArtistSeparators(const QString &separators);
    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool enabled);
    QString batterySaverMode() const { return m_batterySaverMode; }
    bool onBattery() const { return m_onBattery; }
    bool batterySaverActive() const { return m_batterySaverMode == "always" || (m_batterySaverMode == "battery" && m_onBattery); }
    bool reduceEffects() const;
    bool resumeAfterSleep() const { return m_resumeAfterSleep; }
    bool pauseOnDisconnect() const { return m_pauseOnDisconnect; }
    bool highContrast() const { return m_highContrast; }
    bool largeControls() const { return m_largeControls; }
    void setBatterySaverMode(const QString &mode);
    void setOnBattery(bool battery);
    void setResumeAfterSleep(bool enabled);
    void setPauseOnDisconnect(bool enabled);
    void setHighContrast(bool enabled);
    void setLargeControls(bool enabled);
    QVariantList songColumns() const { return m_songColumns; }
    void setSongColumns(const QVariantList &columns);
    Q_INVOKABLE void resetSongColumns();
    Q_INVOKABLE QVariantList defaultSongColumns() const;
    bool columnsCustomized() const;
    bool avoidRecentShuffle() const { return m_avoidRecentShuffle; }
    void setAvoidRecentShuffle(bool enabled);
    int shuffleRecentDays() const { return m_shuffleRecentDays; }
    void setShuffleRecentDays(int days);
    QVariantMap shortcuts() const;
    QVariantList shortcutOptions() const;
    Q_INVOKABLE QString setShortcut(const QString &id, const QString &sequence);
    Q_INVOKABLE QString assignShortcut(const QString &id, int key, int modifiers);
    Q_INVOKABLE void resetShortcuts();
    bool capturingShortcut() const { return m_capturingShortcut; }
    void setCapturingShortcut(bool enabled) { if (m_capturingShortcut == enabled) return; m_capturingShortcut = enabled; emit shortcutCaptureChanged(); }
    Q_INVOKABLE void setArtwork(const QUrl &url);
    Q_INVOKABLE void reset();
    Q_INVOKABLE QVariantMap exportPreferences();
    Q_INVOKABLE void importPreferences(const QVariantMap &preferences);
    Q_INVOKABLE void watchWindow(QWindow *window, bool primary = true);
    Q_INVOKABLE void fitWindow(QWindow *window);
    Q_INVOKABLE void settleLayout(QQuickItem *item);
    static QRect fittedGeometry(const QRect &window, const QRect &available);
signals:
    void systemFontChanged();
    void screenChanged();
    void windowSizeChanged();
    void preferencesChanged();
    void appearanceChanged();
    void seekSecondsChanged();
    void artistSettingsChanged();
    void motionChanged();
    void shortcutsChanged();
    void shortcutCaptureChanged();
    void libraryViewChanged();
    void desktopPreferencesChanged();
    void powerSourceChanged();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void initialize();
    void loadLibraryViewSettings();
    void updateSystemFont();
    void watchScreen(QScreen *screen);
    void save();
    void analyzeArtwork();
    QSettings m_settings;
    QFont m_font;
    QString m_density = QStringLiteral("automatic");
    qreal m_textScale = 1;
    QPointer<QWindow> m_primary;
    QList<QPointer<QWindow>> m_windows;
    QSet<QScreen *> m_screens;
    QTimer m_saveTimer;
    QString m_theme = QStringLiteral("dark");
    bool m_artworkColors = false;
    int m_seekSeconds = 10;
    bool m_splitArtists = false;
    QString m_artistSeparators = QStringLiteral(",;|");
    bool m_reducedMotion = false;
    QString m_batterySaverMode = QStringLiteral("battery");
    bool m_onBattery = false, m_resumeAfterSleep = false, m_pauseOnDisconnect = true;
    bool m_highContrast = false, m_largeControls = false;
    bool m_capturingShortcut = false;
    QVariantMap m_shortcuts;
    QVariantList m_songColumns;
    bool m_avoidRecentShuffle = false;
    int m_shuffleRecentDays = 7;
    QUrl m_artworkUrl, m_readingArtwork;
    QColor m_artworkAccent;
    QCache<QString, QColor> m_artworkCache{128};
    QFutureWatcher<QColor> m_artworkWatcher;
};
