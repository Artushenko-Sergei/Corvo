#include "NotificationManager.h"

#include "Logger.h"
#include "Settings.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QSystemTrayIcon>
#include <QVariantMap>
#include <QWebEngineNotification>
#include <QWebEngineProfile>

namespace {
const char *kService   = "org.freedesktop.Notifications";
const char *kPath      = "/org/freedesktop/Notifications";
const char *kInterface = "org.freedesktop.Notifications";
constexpr char kDefaultAction[] = "default";
} // namespace

NotificationManager::NotificationManager(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    probeDBus();
}

NotificationManager::~NotificationManager()
{
    // The presenter lambda captures `this`; the profile lives longer than this
    // object, so it must not keep calling into freed memory.
    if (m_profile) {
        m_profile->setNotificationPresenter(nullptr);
        qCInfo(logNotify) << "Notification presenter detached from profile"
                          << m_profile->storageName();
    }
    // Drop any notification still on screen: they hold pointers into the page.
    m_active.clear();
    m_tagToId.clear();
    m_lastFallback.reset();
}

void NotificationManager::probeDBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(logNotify) << "Session D-Bus is not available:"
                             << bus.lastError().message();
        m_dbusAvailable = false;
        return;
    }

    QDBusConnectionInterface *iface = bus.interface();
    m_dbusAvailable = iface && iface->isServiceRegistered(QLatin1String(kService));
    if (!m_dbusAvailable) {
        qCWarning(logNotify) << kService
                             << "is not registered; falling back to tray messages";
        return;
    }

    const QDBusMessage call = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface),
        QStringLiteral("GetCapabilities"));
    const QDBusMessage reply = bus.call(call, QDBus::Block, 2000);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        m_capabilities = reply.arguments().constFirst().toStringList();

    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface),
                QStringLiteral("ActionInvoked"), this, SLOT(onActionInvoked(uint, QString)));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface),
                QStringLiteral("NotificationClosed"), this,
                SLOT(onNotificationClosed(uint, uint)));

    qCInfo(logNotify) << "Using org.freedesktop.Notifications, capabilities:" << m_capabilities;
}

void NotificationManager::attachToProfile(QWebEngineProfile *profile)
{
    if (!profile) {
        qCWarning(logNotify) << "attachToProfile() called with a null profile";
        return;
    }

    m_profile = profile;
    profile->setNotificationPresenter(
        [this](std::unique_ptr<QWebEngineNotification> notification) {
            presentWebNotification(std::move(notification));
        });
    qCInfo(logNotify) << "Notification presenter installed on profile"
                      << profile->storageName();
}

void NotificationManager::setTrayFallback(QSystemTrayIcon *tray)
{
    m_tray = tray;
}

void NotificationManager::presentWebNotification(
    std::unique_ptr<QWebEngineNotification> notification)
{
    if (!notification)
        return;

    const QString title = notification->title();
    const QString body = notification->message();
    const QString tag = notification->tag();

    qCInfo(logNotify).noquote() << "Web notification:" << title << "|" << body
                                << "| tag:" << (tag.isEmpty() ? QStringLiteral("<none>") : tag)
                                << "| origin:" << notification->origin().toString();

    if (m_settings && !m_settings->notificationsEnabled()) {
        qCInfo(logNotify) << "Notifications disabled in settings, dropping";
        notification->close();
        return;
    }

    // Tell the page the notification is on screen (fires its onshow handler).
    notification->show();

    std::shared_ptr<QWebEngineNotification> shared(notification.release());

    if (m_dbusAvailable) {
        const uint id = sendToDBus(title, body, tag, -1);
        if (id != 0) {
            m_active.insert(id, shared);
            if (!tag.isEmpty())
                m_tagToId.insert(tag, id);
            connect(shared.get(), &QWebEngineNotification::closed, this, [this, id]() {
                // The page itself withdrew the notification.
                if (!m_active.contains(id))
                    return;
                QDBusConnection::sessionBus().call(
                    QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath),
                                                   QLatin1String(kInterface),
                                                   QStringLiteral("CloseNotification"))
                        << QVariant::fromValue(id),
                    QDBus::NoBlock);
                m_active.remove(id);
            });
            return;
        }
        qCWarning(logNotify) << "Notify() failed, using tray fallback";
    }

    if (m_tray && m_tray->isVisible()) {
        m_lastFallback = shared;
        m_tray->showMessage(title, body, QSystemTrayIcon::Information, 5000);
    } else {
        qCWarning(logNotify) << "No notification transport available; message dropped";
        shared->close();
    }
}

uint NotificationManager::sendToDBus(const QString &title, const QString &body,
                                     const QString &tag, int timeoutMs)
{
    // Reuse the previous id for the same tag so a chat updates in place.
    const uint replacesId = tag.isEmpty() ? 0u : m_tagToId.value(tag, 0u);

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("Corvo"));
    hints.insert(QStringLiteral("category"), QStringLiteral("im.received"));
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uchar>(1));
    hints.insert(QStringLiteral("sound-name"), QStringLiteral("message-new-instant"));

    const QStringList actions{QLatin1String(kDefaultAction), tr("Открыть")};

    QDBusMessage call = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath), QLatin1String(kInterface),
        QStringLiteral("Notify"));
    call << QStringLiteral("Corvo")            // app_name
         << replacesId                             // replaces_id
         << QStringLiteral("Corvo")            // app_icon (themed icon name)
         << (title.isEmpty() ? QStringLiteral("Corvo") : title)
         << body
         << actions
         << hints
         << timeoutMs;

    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qCWarning(logNotify) << "Notify() D-Bus error:" << reply.errorName()
                             << reply.errorMessage();
        return 0;
    }
    if (reply.arguments().isEmpty())
        return 0;

    const uint id = reply.arguments().constFirst().toUInt();
    qCDebug(logNotify) << "Notification shown, id:" << id << "replaces:" << replacesId;
    return id;
}

void NotificationManager::notify(const QString &title, const QString &body, const QString &tag,
                                 int timeoutMs)
{
    qCInfo(logNotify).noquote() << "App notification:" << title << "|" << body;

    if (m_settings && !m_settings->notificationsEnabled())
        return;

    if (m_dbusAvailable) {
        const uint id = sendToDBus(title, body, tag, timeoutMs);
        if (id != 0) {
            if (!tag.isEmpty())
                m_tagToId.insert(tag, id);
            return;
        }
    }
    if (m_tray && m_tray->isVisible())
        m_tray->showMessage(title, body, QSystemTrayIcon::Information, 5000);
}

void NotificationManager::onActionInvoked(uint id, const QString &actionKey)
{
    const auto it = m_active.constFind(id);
    if (it == m_active.cend())
        return;

    qCInfo(logNotify) << "Notification" << id << "activated, action:" << actionKey;
    // Routes the click back into the page, which opens the right chat.
    it.value()->click();
    emit notificationClicked();
}

void NotificationManager::onNotificationClosed(uint id, uint reason)
{
    const auto it = m_active.find(id);
    if (it == m_active.end())
        return;

    qCDebug(logNotify) << "Notification" << id << "closed, reason:" << reason;
    it.value()->close();
    m_active.erase(it);

    for (auto tagIt = m_tagToId.begin(); tagIt != m_tagToId.end();) {
        if (tagIt.value() == id)
            tagIt = m_tagToId.erase(tagIt);
        else
            ++tagIt;
    }
}

void NotificationManager::handleTrayMessageClicked()
{
    if (m_lastFallback) {
        m_lastFallback->click();
        m_lastFallback.reset();
    }
    emit notificationClicked();
}
