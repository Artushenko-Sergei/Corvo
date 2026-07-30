// NotificationManager.h - desktop notifications via org.freedesktop.Notifications.
#ifndef CORVO_NOTIFICATIONMANAGER_H
#define CORVO_NOTIFICATIONMANAGER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class Settings;
class QSystemTrayIcon;
class QWebEngineProfile;
class QWebEngineNotification;

/**
 * Bridges the web Notification API to the Linux desktop.
 *
 * Notifications raised by WhatsApp Web reach us through
 * QWebEngineProfile::setNotificationPresenter(). They are forwarded to
 * org.freedesktop.Notifications over D-Bus, which gives real desktop
 * notifications with actions and replacement-by-tag. When no notification
 * daemon is running the tray icon's balloon message is used instead.
 */
class NotificationManager : public QObject
{
    Q_OBJECT

public:
    explicit NotificationManager(Settings *settings, QObject *parent = nullptr);
    ~NotificationManager() override;

    /// Installs this object as the profile's notification presenter.
    void attachToProfile(QWebEngineProfile *profile);

    /// Optional fallback used when org.freedesktop.Notifications is missing.
    void setTrayFallback(QSystemTrayIcon *tray);

    bool isDBusAvailable() const { return m_dbusAvailable; }
    QStringList serverCapabilities() const { return m_capabilities; }

    /// Shows an application notification (network errors, download finished...).
    void notify(const QString &title, const QString &body,
                const QString &tag = QString(), int timeoutMs = -1);

public slots:
    /// Wire this to QSystemTrayIcon::messageClicked when using the fallback.
    void handleTrayMessageClicked();

signals:
    /// The user clicked a notification: the main window should be raised.
    void notificationClicked();

private slots:
    void onActionInvoked(uint id, const QString &actionKey);
    void onNotificationClosed(uint id, uint reason);

private:
    void presentWebNotification(std::unique_ptr<QWebEngineNotification> notification);
    uint sendToDBus(const QString &title, const QString &body, const QString &tag, int timeoutMs);
    void probeDBus();

    Settings *m_settings = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    /// Profile this object is the notification presenter for. The profile now
    /// outlives this object, so the presenter callback - which captures `this` -
    /// must be removed in the destructor.
    QWebEngineProfile *m_profile = nullptr;
    bool m_dbusAvailable = false;
    QStringList m_capabilities;

    /// Live notifications keyed by the id returned from Notify().
    QHash<uint, std::shared_ptr<QWebEngineNotification>> m_active;
    /// tag -> id, so a chat's notification replaces its predecessor.
    QHash<QString, uint> m_tagToId;
    /// Last web notification shown through the tray fallback.
    std::shared_ptr<QWebEngineNotification> m_lastFallback;
};

#endif // CORVO_NOTIFICATIONMANAGER_H
