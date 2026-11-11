#pragma once

#include <QFont>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QWindow>

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
    Q_INVOKABLE void reset();
    Q_INVOKABLE void watchWindow(QWindow *window, bool primary = true);
    Q_INVOKABLE void fitWindow(QWindow *window);
    static QRect fittedGeometry(const QRect &window, const QRect &available);
signals:
    void systemFontChanged();
    void screenChanged();
    void windowSizeChanged();
    void preferencesChanged();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void initialize();
    void updateSystemFont();
    void watchScreen(QScreen *screen);
    void save();
    QSettings m_settings;
    QFont m_font;
    QString m_density = QStringLiteral("automatic");
    qreal m_textScale = 1;
    QPointer<QWindow> m_primary;
    QList<QPointer<QWindow>> m_windows;
    QSet<QScreen *> m_screens;
    QTimer m_saveTimer;
};
