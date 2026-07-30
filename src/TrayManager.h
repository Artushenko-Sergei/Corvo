// TrayManager.h - system tray icon, context menu and unread badge.
#ifndef CORVO_TRAYMANAGER_H
#define CORVO_TRAYMANAGER_H

#include <QIcon>
#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

#include <memory>

class Settings;
class QAction;
class QMenu;

/**
 * Owns the QSystemTrayIcon and its menu.
 *
 * Emits intent signals only - the window logic itself stays in MainWindow.
 * The icon carries an unread-message badge painted on top of the app icon and
 * the menu shows the current connection status.
 */
class TrayManager : public QObject
{
    Q_OBJECT

public:
    explicit TrayManager(Settings *settings, QObject *parent = nullptr);
    ~TrayManager() override;

    /// False when no StatusNotifier/XEmbed tray is running on this desktop.
    static bool isSystemTrayAvailable();

    bool isActive() const;
    QSystemTrayIcon *trayIcon() const { return m_tray; }

    void setVisible(bool visible);
    /// Rebuilds the context menu after an interface language change.
    void retranslate();
    void setUnreadCount(int count);
    int unreadCount() const { return m_unreadCount; }

    /// Text shown in the menu header and tooltip, e.g. "Подключено".
    void setStatusText(const QString &status);

    /// Keeps the "Скрыть в трей"/"Показать окно" items in sync with the window.
    void setWindowVisible(bool visible);

    void showMessage(const QString &title, const QString &body,
                     QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information,
                     int msecs = 5000);

signals:
    void showWindowRequested();
    void hideWindowRequested();
    void toggleWindowRequested();
    void reloadRequested();
    void settingsRequested();
    void deviceCheckRequested();
    void quitRequested();
    void messageClicked();

private:
    void buildMenu();
    void updateIcon();
    void updateTooltip();
    QIcon baseIcon() const;
    QIcon iconWithBadge(int count) const;
    void onActivated(QSystemTrayIcon::ActivationReason reason);

    Settings *m_settings = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    /// QSystemTrayIcon::setContextMenu() does not take ownership and this class
    /// is not a QWidget, so the menu cannot be parented - own it explicitly.
    std::unique_ptr<QMenu> m_menu;
    // Every action is kept, because retranslate() rewrites their texts in place:
    // swapping the whole menu crashes the D-Bus (StatusNotifier) tray backend
    // inside QDBusTrayIcon::updateMenu().
    QAction *m_statusAction = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_hideAction = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_devicesAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_quitAction = nullptr;
    QString m_statusText;
    int m_unreadCount = 0;
    bool m_windowVisible = true;
};

#endif // CORVO_TRAYMANAGER_H
