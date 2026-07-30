#include "MainWindow.h"

#include "AppInfo.h"
#include "DeviceCheckDialog.h"
#include "Localization.h"
#include "Logger.h"
#include "NotificationManager.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "TrayManager.h"
#include "WebPage.h"
#include "WebProfile.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMoveEvent>
#include <QNetworkInformation>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineFullScreenRequest>
#include <QWebEngineProfile>
#include <QWebEngineView>

namespace {
constexpr int kMaxAutoReloadAttempts = 3;
constexpr int kAutoReloadDelayMs = 5000;
} // namespace

MainWindow::MainWindow(Settings *settings, WebProfile *profile, QWidget *parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_profile(profile)
{
    Q_ASSERT(m_settings && m_profile);

    setObjectName(QStringLiteral("CorvoMainWindow"));
    setWindowTitle(QStringLiteral("Corvo"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral(CORVO_APP_ID),
                                   QIcon(QStringLiteral(":/icons/Corvo.png"))));
    setMinimumSize(600, 480);

    m_geometrySaveTimer = new QTimer(this);
    m_geometrySaveTimer->setSingleShot(true);
    m_geometrySaveTimer->setInterval(400);
    connect(m_geometrySaveTimer, &QTimer::timeout, this, &MainWindow::saveGeometryToSettings);

    // WhatsApp switches its language when a phone is linked, without a reload,
    // so the page is asked again from time to time.
    m_languageProbeTimer = new QTimer(this);
    m_languageProbeTimer->setInterval(20000);
    connect(m_languageProbeTimer, &QTimer::timeout, this, &MainWindow::detectWebLanguage);

    m_notifications = new NotificationManager(m_settings, this);
    m_notifications->attachToProfile(m_profile);
    connect(m_notifications, &NotificationManager::notificationClicked,
            this, &MainWindow::showAndActivate);

    createWebView();
    createMenus();
    createStatusBar();
    createTray();
    connectNetworkMonitor();

    restoreGeometryFromSettings();
    applyZoom();

    connect(m_settings, &Settings::zoomPercentChanged, this, [this](int) { applyZoom(); });
    connect(m_settings, &Settings::notificationsEnabledChanged, this, [this](bool enabled) {
        m_page->setNotificationsAllowed(enabled);
    });
    connect(m_settings, &Settings::trayEnabledChanged, this, [this](bool enabled) {
        if (m_tray)
            m_tray->setVisible(enabled);
    });

    qCInfo(logApp) << "Main window constructed, loading" << whatsAppUrl().toString();
    m_page->load(whatsAppUrl());
}

MainWindow::~MainWindow()
{
    // Nothing to tear down by hand: every page created here (this window's page,
    // the device-check dialog, popup windows) is a descendant of this window and
    // is therefore destroyed with it, while the profile - owned by main() -
    // outlives all of them, as Qt WebEngine requires.
    qCInfo(logApp) << "Main window destroyed";
}

QUrl MainWindow::whatsAppUrl()
{
    return QUrl(QStringLiteral("https://web.whatsapp.com"));
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MainWindow::createWebView()
{
    m_view = new QWebEngineView(this);
    m_page = new WebPage(m_profile, WebPage::UrlPolicy::WhatsAppOnly, m_view);
    m_page->setNotificationsAllowed(m_settings->notificationsEnabled());
    m_view->setPage(m_page);
    m_view->setContextMenuPolicy(Qt::DefaultContextMenu);

    // --- Offline / load-failure page (section 14) --------------------------
    m_errorWidget = new QWidget(this);
    m_errorTitle = new QLabel(m_errorWidget);
    QFont titleFont = m_errorTitle->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    m_errorTitle->setFont(titleFont);
    m_errorTitle->setAlignment(Qt::AlignCenter);

    m_errorDetails = new QLabel(m_errorWidget);
    m_errorDetails->setAlignment(Qt::AlignCenter);
    m_errorDetails->setWordWrap(true);
    m_errorDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *retryButton = new QPushButton(tr("Повторить попытку"), m_errorWidget);
    auto *logsButton = new QPushButton(tr("Открыть журнал"), m_errorWidget);
    connect(retryButton, &QPushButton::clicked, this, &MainWindow::reloadPage);
    connect(logsButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::logPath()));
    });

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(retryButton);
    buttonRow->addWidget(logsButton);
    buttonRow->addStretch(1);

    auto *errorLayout = new QVBoxLayout(m_errorWidget);
    errorLayout->addStretch(1);
    errorLayout->addWidget(m_errorTitle);
    errorLayout->addSpacing(8);
    errorLayout->addWidget(m_errorDetails);
    errorLayout->addSpacing(16);
    errorLayout->addLayout(buttonRow);
    errorLayout->addStretch(2);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_view);
    m_stack->addWidget(m_errorWidget);
    setCentralWidget(m_stack);

    connect(m_page, &QWebEnginePage::loadStarted, this, &MainWindow::handleLoadStarted);
    connect(m_page, &QWebEnginePage::loadProgress, this, &MainWindow::handleLoadProgress);
    connect(m_page, &QWebEnginePage::loadFinished, this, &MainWindow::handleLoadFinished);
    connect(m_page, &QWebEnginePage::titleChanged, this, &MainWindow::handleTitleChanged);
    connect(m_page, &QWebEnginePage::fullScreenRequested,
            this, &MainWindow::handleFullScreenRequested);
    connect(m_page, &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus status, int exitCode) {
                showErrorPage(tr("Процесс WebEngine завершился"),
                              tr("Статус: %1, код выхода: %2.\n"
                                 "Нажмите «Повторить попытку», чтобы перезапустить страницу.")
                                  .arg(static_cast<int>(status))
                                  .arg(exitCode));
            });

    // Permissions and external links are interesting for the log, not for the
    // user's status bar; only a blocked navigation is worth surfacing.
    connect(m_page, &WebPage::navigationBlocked, this, [this](const QUrl &url) {
        showStatus(tr("Переход заблокирован: %1").arg(url.toString()));
    });

    connect(m_profile, &WebProfile::downloadStarted, this,
            [this](const QString &fileName, const QString &) {
                showStatus(tr("Загрузка файла: %1").arg(fileName), 0);
            });
    connect(m_profile, &WebProfile::downloadFinished, this,
            [this](const QString &fileName, bool ok) {
                showStatus(ok ? tr("Файл сохранён: %1").arg(fileName)
                              : tr("Ошибка загрузки: %1").arg(fileName));
                if (m_notifications) {
                    m_notifications->notify(ok ? tr("Файл загружен") : tr("Ошибка загрузки"),
                                            fileName, QStringLiteral("download"));
                }
            });
}

void MainWindow::createMenus()
{
    // Called again on a language change: drop the previous menus first, or they
    // would accumulate as hidden children of the menu bar.
    const QList<QMenu *> previous = menuBar()->findChildren<QMenu *>(Qt::FindDirectChildrenOnly);
    menuBar()->clear();
    qDeleteAll(previous);

    // --- Файл --------------------------------------------------------------
    QMenu *fileMenu = menuBar()->addMenu(tr("&Файл"));

    QAction *openProfile = fileMenu->addAction(tr("Открыть профиль"));
    openProfile->setStatusTip(tr("Открыть каталог с сессией WhatsApp"));
    connect(openProfile, &QAction::triggered, this, [this]() {
        const QString path = m_settings->profilePath();
        qCInfo(logApp) << "Opening profile directory" << path;
        QDir().mkpath(path);
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            QMessageBox::information(this, tr("Профиль"),
                                     tr("Каталог профиля:\n%1").arg(path));
        }
    });

    QAction *openLogs = fileMenu->addAction(tr("Открыть журнал"));
    connect(openLogs, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::logPath()));
    });

    fileMenu->addSeparator();

    QAction *settingsAction = fileMenu->addAction(tr("Настройки..."));
    settingsAction->setShortcut(QKeySequence::Preferences);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsDialog);

    fileMenu->addSeparator();

    QAction *quitAction = fileMenu->addAction(tr("Выход"));
    quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(quitAction, &QAction::triggered, this, &MainWindow::quitApplication);

    // --- Вид ---------------------------------------------------------------
    QMenu *viewMenu = menuBar()->addMenu(tr("&Вид"));

    QAction *showAction = viewMenu->addAction(tr("Показать окно"));
    connect(showAction, &QAction::triggered, this, &MainWindow::showAndActivate);

    m_hideAction = viewMenu->addAction(tr("Скрыть в трей"));
    m_hideAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
    connect(m_hideAction, &QAction::triggered, this, &MainWindow::hideToTray);

    viewMenu->addSeparator();

    m_reloadAction = viewMenu->addAction(tr("Перезагрузить"));
    m_reloadAction->setShortcut(QKeySequence::Refresh);
    connect(m_reloadAction, &QAction::triggered, this, &MainWindow::reloadPage);

    m_fullScreenAction = viewMenu->addAction(tr("Полный экран"));
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    connect(m_fullScreenAction, &QAction::toggled, this, [this](bool on) {
        if (on) {
            showFullScreen();
        } else {
            m_wasMaximized ? showMaximized() : showNormal();
        }
    });

    viewMenu->addSeparator();

    QAction *zoomIn = viewMenu->addAction(tr("Увеличить масштаб"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, this,
            [this]() { m_settings->setZoomPercent(m_settings->zoomPercent() + 10); });

    QAction *zoomOut = viewMenu->addAction(tr("Уменьшить масштаб"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this,
            [this]() { m_settings->setZoomPercent(m_settings->zoomPercent() - 10); });

    QAction *zoomReset = viewMenu->addAction(tr("Сбросить масштаб"));
    zoomReset->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(zoomReset, &QAction::triggered, this,
            [this]() { m_settings->setZoomPercent(100); });

    // --- Помощь ------------------------------------------------------------
    QMenu *helpMenu = menuBar()->addMenu(tr("&Помощь"));

    QAction *aboutAction = helpMenu->addAction(tr("О программе"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout()
{
    // Short and welcoming: what the program is, who made it, and that it is
    // free software. Trademark and packaging details stay in the documentation.
    const QString full = QApplication::applicationVersion();
    const QString version = full.section(QLatin1Char('+'), 0, 0);
    const QString build = full.section(QLatin1Char('+'), 1);
    const QString versionLine = build.isEmpty()
                                    ? tr("Версия %1").arg(version)
                                    : tr("Версия %1 (сборка %2)").arg(version, build);

    QMessageBox about(this);
    about.setWindowTitle(tr("О программе"));
    about.setIconPixmap(windowIcon().pixmap(64, 64));
    about.setTextFormat(Qt::RichText);
    about.setText(QStringLiteral("<h3 style=\"margin:0\">Corvo</h3>"
                                 "<p style=\"margin:2px 0 10px 0; color:palette(mid)\">%1</p>"
                                 "<p style=\"margin:0\">%2</p>")
                      .arg(versionLine,
                           tr("WhatsApp на рабочем столе: чаты, звонки и уведомления "
                              "в отдельном окне.")));
    about.setInformativeText(
        QStringLiteral("<p style=\"margin:0 0 6px 0\">%1</p><p style=\"margin:0\">%2</p>")
            .arg(tr("Разработчик: %1").arg(tr("Сергей Артюшенко")),
                 tr("Работает на Qt %1. Свободная программа с открытым исходным кодом: "
                    "её можно бесплатно использовать, изучать и передавать другим.")
                     .arg(QLatin1String(qVersion()))));
    about.setStandardButtons(QMessageBox::Close);
    about.button(QMessageBox::Close)->setText(tr("Закрыть"));
    about.exec();
}

void MainWindow::createStatusBar()
{
    m_statusLabel = new QLabel(this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setMaximumWidth(160);
    m_progress->setTextVisible(false);
    m_progress->hide();

    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_progress);
    statusBar()->setSizeGripEnabled(false);
    // Nothing to report at rest: the bar appears only for load progress,
    // network problems, downloads and blocked navigation.
    statusBar()->hide();

    m_statusHideTimer = new QTimer(this);
    m_statusHideTimer->setSingleShot(true);
    connect(m_statusHideTimer, &QTimer::timeout, this, &MainWindow::hideStatus);
}

void MainWindow::createTray()
{
    m_tray = new TrayManager(m_settings, this);
    if (!m_tray->trayIcon()) {
        qCWarning(logApp) << "Running without tray support; closing the window will quit";
        return;
    }

    m_notifications->setTrayFallback(m_tray->trayIcon());
    connect(m_tray, &TrayManager::messageClicked,
            m_notifications, &NotificationManager::handleTrayMessageClicked);

    connect(m_tray, &TrayManager::showWindowRequested, this, &MainWindow::showAndActivate);
    connect(m_tray, &TrayManager::hideWindowRequested, this, &MainWindow::hideToTray);
    connect(m_tray, &TrayManager::toggleWindowRequested, this, &MainWindow::toggleWindow);
    connect(m_tray, &TrayManager::reloadRequested, this, &MainWindow::reloadPage);
    connect(m_tray, &TrayManager::settingsRequested, this, &MainWindow::openSettingsDialog);
    connect(m_tray, &TrayManager::deviceCheckRequested, this, &MainWindow::openDeviceCheck);
    connect(m_tray, &TrayManager::quitRequested, this, &MainWindow::quitApplication);

    m_tray->setVisible(m_settings->trayEnabled());
    m_tray->setStatusText(tr("Загрузка WhatsApp..."));
}

void MainWindow::connectNetworkMonitor()
{
    if (!QNetworkInformation::loadDefaultBackend()) {
        qCWarning(logApp) << "No QNetworkInformation backend; offline detection limited";
        return;
    }
    auto *info = QNetworkInformation::instance();
    if (!info)
        return;

    qCInfo(logApp) << "Network backend:" << info->backendName()
                   << "reachability:" << static_cast<int>(info->reachability());

    connect(info, &QNetworkInformation::reachabilityChanged, this,
            [this](QNetworkInformation::Reachability reachability) {
                const bool online = reachability == QNetworkInformation::Reachability::Online;
                qCInfo(logApp) << "Reachability changed, online:" << online;
                if (!online) {
                    showStatus(tr("Нет подключения к сети"), 0);
                    if (m_tray)
                        m_tray->setStatusText(tr("Нет сети"));
                    return;
                }
                hideStatus();
                if (m_stack->currentWidget() == m_errorWidget) {
                    m_reloadAttempts = 0;
                    reloadPage();
                }
            });
}

// ---------------------------------------------------------------------------
// Geometry (section 3)
// ---------------------------------------------------------------------------

bool MainWindow::positionPersistenceSupported()
{
    // On Wayland a client cannot position itself: move() is ignored and pos()
    // returns values derived from the output layout (e.g. the origin of the
    // monitor the compositor chose). Storing those would overwrite a perfectly
    // good X11 position with a number that means nothing on the next start.
    return !QGuiApplication::platformName().startsWith(QLatin1String("wayland"),
                                                      Qt::CaseInsensitive);
}

void MainWindow::restoreGeometryFromSettings()
{
    const QSize size = m_settings->windowSize();
    resize(size);

    QPoint pos;
    if (!positionPersistenceSupported()) {
        qCInfo(logApp) << "Platform" << QGuiApplication::platformName()
                       << "does not support client-side window positioning; "
                          "only the window size is persisted";
    } else if (m_settings->hasWindowPosition()) {
        pos = m_settings->windowPosition();
        // Only restore the position if it still lands on an existing screen.
        // Negative coordinates are valid: a monitor may sit left of / above the
        // primary one.
        bool onScreen = false;
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (const QScreen *screen : screens) {
            if (screen->availableGeometry().contains(pos)) {
                onScreen = true;
                break;
            }
        }
        if (onScreen) {
            move(pos);
        } else {
            qCWarning(logApp) << "Stored window position" << pos
                              << "is off-screen, ignoring";
            m_settings->clearWindowPosition();
        }
    }

    m_wasMaximized = m_settings->windowMaximized();
    if (m_wasMaximized)
        showMaximized();

    qCInfo(logApp) << "Geometry restored:" << size << pos << "maximized:" << m_wasMaximized;
}

void MainWindow::scheduleGeometrySave()
{
    if (m_geometrySaveTimer)
        m_geometrySaveTimer->start();
}

void MainWindow::saveGeometryToSettings()
{
    if (m_geometrySaveTimer)
        m_geometrySaveTimer->stop();

    const bool maximized = isMaximized() || isFullScreen();
    m_settings->setWindowMaximized(maximized);
    if (!maximized) {
        m_settings->setWindowSize(size());
        if (positionPersistenceSupported())
            m_settings->setWindowPosition(pos());
    }
    m_settings->sync();
    qCDebug(logApp) << "Geometry saved:" << size() << pos() << "maximized:" << maximized;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Writing on every event would mean thousands of QSettings updates during a
    // single drag, so the write is collapsed into one deferred call.
    if (!isMaximized() && !isFullScreen() && isVisible())
        scheduleGeometrySave();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    if (!isMaximized() && !isFullScreen() && isVisible() && positionPersistenceSupported())
        scheduleGeometrySave();
}

// ---------------------------------------------------------------------------
// Window / tray behaviour (section 9)
// ---------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveGeometryToSettings();

    const bool trayReady = m_tray && m_tray->isActive();
    if (!m_forceQuit && m_settings->closeToTray() && trayReady) {
        event->ignore();
        hide();
        m_tray->setWindowVisible(false);
        qCInfo(logApp) << "Window hidden to tray instead of quitting";
        return;
    }

    if (!trayReady && !m_forceQuit)
        qCInfo(logApp) << "No tray available, closing quits the application";

    qCInfo(logApp) << "Shutting down";
    event->accept();
    QApplication::quit();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);

    if (event->type() == QEvent::LanguageChange) {
        // Rebuilding the menus deletes widgets, which must not happen while Qt is
        // still dispatching this event - doing it inline segfaults. Defer it, and
        // coalesce the two events that a translator swap produces.
        if (!m_retranslateScheduled) {
            m_retranslateScheduled = true;
            QTimer::singleShot(0, this, &MainWindow::retranslateUi);
        }
        return;
    }
    if (event->type() != QEvent::WindowStateChange)
        return;

    if (m_fullScreenAction)
        m_fullScreenAction->setChecked(isFullScreen());

    if (isMinimized() && m_settings->minimizeToTray() && m_tray && m_tray->isActive()) {
        // Defer, because hiding from inside the state-change handler upsets
        // some window managers.
        QTimer::singleShot(0, this, &MainWindow::hideToTray);
    } else if (!isMinimized() && !isFullScreen()) {
        m_wasMaximized = isMaximized();
    }
}

void MainWindow::showAndActivate()
{
    if (isMinimized())
        setWindowState(windowState() & ~Qt::WindowMinimized);
    if (!isVisible()) {
        m_wasMaximized ? showMaximized() : showNormal();
    }
    raise();
    activateWindow();
    if (m_tray)
        m_tray->setWindowVisible(true);
    qCDebug(logApp) << "Window shown and activated";
}

void MainWindow::hideToTray()
{
    if (!m_tray || !m_tray->isActive()) {
        QMessageBox::information(this, tr("Системный трей"),
                                 tr("Системный трей недоступен, окно нельзя скрыть."));
        return;
    }
    saveGeometryToSettings();
    hide();
    m_tray->setWindowVisible(false);
    qCInfo(logApp) << "Window hidden to tray";
}

void MainWindow::toggleWindow()
{
    if (isVisible() && !isMinimized())
        hideToTray();
    else
        showAndActivate();
}

void MainWindow::quitApplication()
{
    qCInfo(logApp) << "Quit requested by user";
    m_forceQuit = true;
    // closeEvent() persists the geometry - no need to do it twice.
    close();
}

// ---------------------------------------------------------------------------
// Page state
// ---------------------------------------------------------------------------

void MainWindow::reloadPage()
{
    qCInfo(logApp) << "Reloading page";
    showWebView();
    if (m_page->url().isEmpty() || !WebPage::isWhatsAppMainUrl(m_page->url()))
        m_page->load(whatsAppUrl());
    else
        m_page->triggerAction(QWebEnginePage::Reload);
}

void MainWindow::applyZoom()
{
    const qreal factor = m_settings->zoomPercent() / 100.0;
    m_view->setZoomFactor(factor);
    qCDebug(logApp) << "Zoom factor:" << factor;
}

void MainWindow::showWebView()
{
    m_stack->setCurrentWidget(m_view);
}

void MainWindow::showErrorPage(const QString &title, const QString &details)
{
    m_errorTitle->setText(title);
    m_errorDetails->setText(details);
    m_stack->setCurrentWidget(m_errorWidget);
    showStatus(title, 0);
    if (m_tray)
        m_tray->setStatusText(title);
    qCWarning(logApp).noquote() << "Error page:" << title << "-" << details;
}

void MainWindow::handleLoadStarted()
{
    m_progress->setValue(0);
    m_progress->show();
    statusBar()->show();
    if (m_tray)
        m_tray->setStatusText(tr("Загрузка..."));
}

void MainWindow::handleLoadProgress(int progress)
{
    m_progress->setValue(progress);
}

void MainWindow::handleLoadFinished(bool ok)
{
    m_progress->hide();

    if (ok) {
        m_reloadAttempts = 0;
        showWebView();
        detectWebLanguage();
        m_languageProbeTimer->start();
        // Done - no "loaded successfully" chatter, just get out of the way.
        hideStatus();
        statusBar()->hide();
        if (m_tray)
            m_tray->setStatusText(tr("Подключено"));
        qCInfo(logApp) << "Page loaded:" << m_page->url().toString();
        return;
    }

    // Empty/aborted loads happen on internal redirects; ignore those.
    if (m_page->url().isEmpty())
        return;

    auto *info = QNetworkInformation::instance();
    const bool offline = info
                         && info->reachability() != QNetworkInformation::Reachability::Online
                         && info->reachability() != QNetworkInformation::Reachability::Unknown;

    ++m_reloadAttempts;
    const QString title = offline ? tr("Нет подключения к сети")
                                  : tr("Не удалось загрузить WhatsApp Web");
    const QString details = offline
        ? tr("Проверьте сетевое подключение. Попытка %1 из %2 - "
             "приложение повторит загрузку автоматически.")
              .arg(m_reloadAttempts).arg(kMaxAutoReloadAttempts)
        : tr("Адрес: %1\nПопытка %2 из %3. Подробности в журнале: %4")
              .arg(m_page->url().toString())
              .arg(m_reloadAttempts).arg(kMaxAutoReloadAttempts)
              .arg(Logger::logFilePath());

    showErrorPage(title, details);

    if (m_reloadAttempts <= kMaxAutoReloadAttempts) {
        qCInfo(logApp) << "Scheduling automatic reload in" << kAutoReloadDelayMs << "ms";
        QTimer::singleShot(kAutoReloadDelayMs, this, [this]() {
            if (m_stack->currentWidget() == m_errorWidget)
                reloadPage();
        });
    } else {
        qCWarning(logApp) << "Automatic reload limit reached; waiting for the user";
    }
}

void MainWindow::handleTitleChanged(const QString &title)
{
    // The window title stays "Corvo" on purpose - the page title is only
    // used to read the unread-message counter out of it.
    updateUnreadFromTitle(title);
}

/// WhatsApp Web encodes the unread count in the document title, e.g. "(3) WhatsApp".
void MainWindow::updateUnreadFromTitle(const QString &title)
{
    static const QRegularExpression re(QStringLiteral("\\((\\d+)\\)"));
    const QRegularExpressionMatch match = re.match(title);
    const int count = match.hasMatch() ? match.captured(1).toInt() : 0;

    if (count == m_unreadCount)
        return;
    m_unreadCount = count;

    if (m_tray)
        m_tray->setUnreadCount(count);
    qCInfo(logApp) << "Unread count:" << count;
}

/**
 * WhatsApp Web exposes the language it renders in through <html lang>, which is
 * the only reliable source: its own language comes from the linked phone and
 * cannot be set from here. In "auto" mode the interface follows it, so the whole
 * window speaks one language.
 */
void MainWindow::detectWebLanguage()
{
    if (!m_page)
        return;

    m_page->runJavaScript(
        QStringLiteral("document.documentElement.lang || navigator.language || ''"),
        [this](const QVariant &value) {
            const QString tag = value.toString();
            if (tag.isEmpty())
                return;

            const QString code = Localization::matchSupported(tag);
            if (code.isEmpty()) {
                qCDebug(logApp) << "WhatsApp language" << tag << "has no interface translation";
                return;
            }

            m_settings->setDetectedWebLanguage(code);
            if (!m_settings->followsWebLanguage())
                return;
            if (code == Localization::currentLanguage())
                return;

            qCInfo(logApp) << "Following WhatsApp interface language:" << code
                           << "(page reported" << tag << ")";
            Localization::apply(QCoreApplication::instance(), code);
            // Qt delivers QEvent::LanguageChange to every widget; changeEvent()
            // turns that into retranslateUi().
        });
}

void MainWindow::retranslateUi()
{
    m_retranslateScheduled = false;
    createMenus();          // rebuilds the menu bar with the new translations
    if (m_tray) {
        m_tray->retranslate();
        if (m_stack->currentWidget() == m_view)
            m_tray->setStatusText(tr("Подключено"));
    }
    if (m_errorWidget && m_stack->currentWidget() == m_errorWidget) {
        // Re-show the current error so its texts follow the new language.
        showErrorPage(tr("Не удалось загрузить WhatsApp Web"), m_errorDetails->text());
    }
    qCInfo(logApp) << "Interface retranslated to" << Localization::currentLanguage();
}

void MainWindow::handleFullScreenRequested(QWebEngineFullScreenRequest request)
{
    request.accept();
    if (request.toggleOn()) {
        m_wasMaximized = isMaximized();
        showFullScreen();
    } else {
        m_wasMaximized ? showMaximized() : showNormal();
    }
    qCInfo(logApp) << "Fullscreen request accepted, on:" << request.toggleOn();
}

void MainWindow::showStatus(const QString &text, int msecs)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setText(text);
    statusBar()->show();
    if (msecs > 0)
        m_statusHideTimer->start(msecs);
    else
        m_statusHideTimer->stop();
}

void MainWindow::hideStatus()
{
    if (!m_statusLabel)
        return;
    m_statusHideTimer->stop();
    m_statusLabel->clear();
    if (!m_progress->isVisible())
        statusBar()->hide();
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

void MainWindow::openDeviceCheck()
{
    if (!m_deviceCheck) {
        m_deviceCheck = new DeviceCheckDialog(m_profile, this);
        m_deviceCheck->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_deviceCheck, &QObject::destroyed, this,
                [this]() { m_deviceCheck = nullptr; });
    }
    m_deviceCheck->show();
    m_deviceCheck->raise();
    m_deviceCheck->activateWindow();
    m_deviceCheck->runEnumeration();
    qCInfo(logMedia) << "Device check dialog opened";
}

void MainWindow::openSettingsDialog()
{
    SettingsDialog dialog(m_settings, this);
    // The camera/microphone check lives in the settings dialog now.
    connect(&dialog, &SettingsDialog::deviceCheckRequested,
            this, &MainWindow::openDeviceCheck);
    connect(&dialog, &SettingsDialog::settingsApplied, this, [this]() {
        applyZoom();
        m_page->setNotificationsAllowed(m_settings->notificationsEnabled());
        if (m_tray)
            m_tray->setVisible(m_settings->trayEnabled());
        // Only the settings Chromium accepts on a live profile; storage/cache
        // paths intentionally require a restart.
        m_profile->applyRuntimeConfiguration();
    });
    dialog.exec();
}
