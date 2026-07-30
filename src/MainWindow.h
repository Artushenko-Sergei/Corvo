// MainWindow.h - application main window (web view, menus, tray integration).
#ifndef CORVO_MAINWINDOW_H
#define CORVO_MAINWINDOW_H

#include <QMainWindow>
#include <QPoint>
#include <QSize>
#include <QUrl>

class DeviceCheckDialog;
class NotificationManager;
class Settings;
class TrayManager;
class WebPage;
class WebProfile;

class QAction;
class QWebEngineFullScreenRequest;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class QWebEngineView;

/**
 * The application window: a QWebEngineView showing https://web.whatsapp.com
 * wrapped in normal desktop-application chrome (menu bar, status bar, tray).
 *
 * The web profile is NOT owned here: it is created in main() and passed in, so
 * that it always outlives this window and every page created from it (a Qt
 * WebEngine requirement).
 *
 * Window geometry is restored from and stored to Settings; closing the window
 * hides it into the tray instead of quitting (section 9).
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Settings *settings, WebProfile *profile, QWidget *parent = nullptr);
    ~MainWindow() override;

    /// URL of WhatsApp Web - the only page the main frame may show.
    static QUrl whatsAppUrl();

    /**
     * True when the platform lets a client read back and restore its own window
     * position. Wayland does not: move() is ignored and pos() reports numbers
     * that cannot be reused, so persisting them would corrupt a good X11 value.
     */
    static bool positionPersistenceSupported();

public slots:
    /// Shows, un-minimises and raises the window (tray/notification clicks).
    void showAndActivate();
    void hideToTray();
    void toggleWindow();
    void reloadPage();
    void openDeviceCheck();
    void openSettingsDialog();
    /// Short, friendly "About" dialog: name, version, purpose.
    void showAbout();
    /// Real quit, as opposed to "close to tray".
    void quitApplication();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    void createWebView();
    void createMenus();
    void createStatusBar();
    void createTray();
    void restoreGeometryFromSettings();
    /// Queues a geometry write; several move/resize events collapse into one.
    void scheduleGeometrySave();
    /// Writes the pending geometry to Settings immediately.
    void saveGeometryToSettings();
    void applyZoom();
    void connectNetworkMonitor();

    void showErrorPage(const QString &title, const QString &details);
    void showWebView();

    /// Shows a message in the status bar. The bar is hidden while there is
    /// nothing worth reporting; msecs == 0 keeps the message until replaced.
    void showStatus(const QString &text, int msecs = 6000);
    void hideStatus();

    void handleLoadStarted();
    void handleLoadProgress(int progress);
    void handleLoadFinished(bool ok);
    void handleTitleChanged(const QString &title);
    /// Asks the page which language it renders in and follows it in "auto" mode.
    void detectWebLanguage();
    /// Rebuilds every translated piece of the window after a language change.
    void retranslateUi();
    void handleFullScreenRequested(QWebEngineFullScreenRequest request);
    void updateUnreadFromTitle(const QString &title);

    Settings *m_settings = nullptr;
    WebProfile *m_profile = nullptr;   ///< owned by main(), never by this window
    WebPage *m_page = nullptr;
    QWebEngineView *m_view = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_errorWidget = nullptr;
    QLabel *m_errorTitle = nullptr;
    QLabel *m_errorDetails = nullptr;

    TrayManager *m_tray = nullptr;
    NotificationManager *m_notifications = nullptr;
    DeviceCheckDialog *m_deviceCheck = nullptr;

    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QTimer *m_statusHideTimer = nullptr;

    QAction *m_reloadAction = nullptr;
    QAction *m_hideAction = nullptr;
    QAction *m_fullScreenAction = nullptr;

    /// Collapses a burst of move/resize events into a single settings write.
    QTimer *m_geometrySaveTimer = nullptr;
    /// Re-checks the page language: WhatsApp switches it after the phone links,
    /// without reloading the page.
    QTimer *m_languageProbeTimer = nullptr;

    /// Guards against retranslating twice: removing and installing a translator
    /// each deliver their own LanguageChange event.
    bool m_retranslateScheduled = false;
    int m_unreadCount = 0;
    bool m_forceQuit = false;
    bool m_wasMaximized = false;
    int m_reloadAttempts = 0;
};

#endif // CORVO_MAINWINDOW_H
