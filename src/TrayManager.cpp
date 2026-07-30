#include "TrayManager.h"

#include "AppInfo.h"
#include "Logger.h"
#include "Settings.h"

#include <QAction>
#include <QApplication>
#include <QFont>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

TrayManager::TrayManager(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_statusText(tr("Запуск..."))
{
    if (!isSystemTrayAvailable()) {
        qCWarning(logTray) << "System tray is not available on this desktop; "
                              "the application will keep a normal window only";
        return;
    }

    m_tray = new QSystemTrayIcon(this);
    buildMenu();
    m_tray->setContextMenu(m_menu.get());
    updateIcon();
    updateTooltip();

    connect(m_tray, &QSystemTrayIcon::activated, this, &TrayManager::onActivated);
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayManager::messageClicked);

    qCInfo(logTray) << "Tray icon created";
}

TrayManager::~TrayManager()
{
    // Detach the menu before it is destroyed: the tray icon keeps a raw pointer
    // and lives on until this QObject's children are deleted.
    if (m_tray) {
        m_tray->setContextMenu(nullptr);
        m_tray->hide();
    }
}

bool TrayManager::isSystemTrayAvailable()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

bool TrayManager::isActive() const
{
    return m_tray != nullptr && m_tray->isVisible();
}

void TrayManager::buildMenu()
{
    m_menu = std::make_unique<QMenu>();

    m_statusAction = m_menu->addAction(m_statusText);
    m_statusAction->setEnabled(false);
    QFont statusFont = m_statusAction->font();
    statusFont.setBold(true);
    m_statusAction->setFont(statusFont);

    m_menu->addSeparator();

    m_showAction = m_menu->addAction(tr("Показать окно"));
    connect(m_showAction, &QAction::triggered, this, &TrayManager::showWindowRequested);

    m_hideAction = m_menu->addAction(tr("Скрыть в трей"));
    connect(m_hideAction, &QAction::triggered, this, &TrayManager::hideWindowRequested);

    m_menu->addSeparator();

    m_reloadAction = m_menu->addAction(tr("Перезагрузить WhatsApp"));
    connect(m_reloadAction, &QAction::triggered, this, &TrayManager::reloadRequested);

    m_devicesAction = m_menu->addAction(tr("Проверка камеры"));
    connect(m_devicesAction, &QAction::triggered, this, &TrayManager::deviceCheckRequested);

    m_settingsAction = m_menu->addAction(tr("Настройки..."));
    connect(m_settingsAction, &QAction::triggered, this, &TrayManager::settingsRequested);

    m_menu->addSeparator();

    m_quitAction = m_menu->addAction(tr("Выход"));
    connect(m_quitAction, &QAction::triggered, this, &TrayManager::quitRequested);
}

void TrayManager::retranslate()
{
    if (!m_menu)
        return;
    // Only the texts are rewritten. Recreating the menu and handing it to
    // QSystemTrayIcon::setContextMenu() again crashes the D-Bus tray backend,
    // which keeps a pointer to the platform menu of the previous QMenu.
    if (m_showAction)
        m_showAction->setText(tr("Показать окно"));
    if (m_hideAction)
        m_hideAction->setText(tr("Скрыть в трей"));
    if (m_reloadAction)
        m_reloadAction->setText(tr("Перезагрузить WhatsApp"));
    if (m_devicesAction)
        m_devicesAction->setText(tr("Проверка камеры"));
    if (m_settingsAction)
        m_settingsAction->setText(tr("Настройки..."));
    if (m_quitAction)
        m_quitAction->setText(tr("Выход"));

    updateTooltip();
    qCInfo(logTray) << "Tray menu retranslated";
}

void TrayManager::setVisible(bool visible)
{
    if (!m_tray)
        return;
    m_tray->setVisible(visible);
    qCInfo(logTray) << "Tray icon visible:" << visible;
}

void TrayManager::setUnreadCount(int count)
{
    count = qMax(0, count);
    if (m_unreadCount == count)
        return;
    m_unreadCount = count;
    qCInfo(logTray) << "Unread messages:" << count;
    updateIcon();
    updateTooltip();
}

void TrayManager::setStatusText(const QString &status)
{
    if (m_statusText == status)
        return;
    m_statusText = status;
    if (m_statusAction)
        m_statusAction->setText(status);
    updateTooltip();
    qCInfo(logTray) << "Status:" << status;
}

void TrayManager::setWindowVisible(bool visible)
{
    m_windowVisible = visible;
    if (m_showAction)
        m_showAction->setEnabled(!visible);
    if (m_hideAction)
        m_hideAction->setEnabled(visible);
}

void TrayManager::showMessage(const QString &title, const QString &body,
                              QSystemTrayIcon::MessageIcon icon, int msecs)
{
    if (!m_tray || !m_tray->isVisible())
        return;
    m_tray->showMessage(title, body, icon, msecs);
}

QIcon TrayManager::baseIcon() const
{
    // Prefer the icon installed into the theme; fall back to the compiled-in one.
    QIcon icon = QIcon::fromTheme(QStringLiteral(CORVO_APP_ID));
    if (icon.isNull())
        icon = QIcon(QStringLiteral(":/icons/Corvo.png"));
    if (icon.isNull())
        icon = QApplication::windowIcon();
    return icon;
}

QIcon TrayManager::iconWithBadge(int count) const
{
    const QIcon base = baseIcon();
    if (count <= 0)
        return base;

    constexpr int kSize = 64;
    QPixmap pixmap = base.pixmap(kSize, kSize);
    if (pixmap.isNull())
        return base;

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QString text = count > 99 ? QStringLiteral("99+") : QString::number(count);
    const int diameter = text.size() > 2 ? 38 : 30;
    const QRect badge(pixmap.width() - diameter - 1, 1, diameter, 28);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(220, 40, 40));
    painter.drawRoundedRect(badge, 14, 14);

    QFont font = painter.font();
    font.setPixelSize(18);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, text);
    painter.end();

    return QIcon(pixmap);
}

void TrayManager::updateIcon()
{
    if (!m_tray)
        return;
    m_tray->setIcon(iconWithBadge(m_unreadCount));
}

void TrayManager::updateTooltip()
{
    if (!m_tray)
        return;
    QString tip = QStringLiteral("Corvo\n%1").arg(m_statusText);
    if (m_unreadCount > 0)
        tip += tr("\nНепрочитанных: %1").arg(m_unreadCount);
    m_tray->setToolTip(tip);
}

void TrayManager::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    qCDebug(logTray) << "Tray activated, reason:" << reason;
    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::MiddleClick:
        emit toggleWindowRequested();
        break;
    case QSystemTrayIcon::DoubleClick:
        emit showWindowRequested();
        break;
    default:
        break;
    }
}
