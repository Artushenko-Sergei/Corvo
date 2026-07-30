// WebPage.h - QWebEnginePage subclass: permissions, URL policy, logging.
#ifndef CORVO_WEBPAGE_H
#define CORVO_WEBPAGE_H

#include <QUrl>
#include <QWebEnginePage>
#include <QWebEnginePermission>

class Settings;

/**
 * Application web page.
 *
 * Responsibilities:
 *  - grants camera/microphone (WebRTC) and notification permissions, logging
 *    every single request through qCInfo(logPermission);
 *  - restricts navigation to https://web.whatsapp.com (section 8), sends any
 *    other http(s) link to the system browser and blocks file:// outright;
 *  - forwards JS console output into the application log;
 *  - opens WhatsApp popups (e.g. group calls) in a popup window owned by the
 *    main window and everything else in the system browser.
 *
 * Permissions use the QWebEnginePermission API (Qt >= 6.8): the
 * permissionRequested() signal delivers a request object that carries
 * grant()/deny(). Note it is a signal, not a virtual function - the same was
 * true of the older featurePermissionRequested() - so the handling lives in this
 * QWebEnginePage subclass by connecting to its own signal.
 */
class WebPage : public QWebEnginePage
{
    Q_OBJECT

public:
    /// Which URLs the page may navigate to.
    enum class UrlPolicy {
        WhatsAppOnly,     ///< main window: only https://web.whatsapp.com
        LocalDiagnostics  ///< device-check dialog: only the bundled qrc: page
    };

    explicit WebPage(QWebEngineProfile *profile,
                     UrlPolicy policy = UrlPolicy::WhatsAppOnly,
                     QObject *parent = nullptr);

    void setNotificationsAllowed(bool allowed) { m_notificationsAllowed = allowed; }
    bool notificationsAllowed() const { return m_notificationsAllowed; }

    /// Human readable name of a permission type (used in logs and in the UI).
    static QString permissionName(QWebEnginePermission::PermissionType type);

    /// True for https://web.whatsapp.com (the only allowed main-frame origin).
    static bool isWhatsAppMainUrl(const QUrl &url);
    /// True for any *.whatsapp.com / *.whatsapp.net host (sub-frames, media).
    static bool isWhatsAppHost(const QUrl &url);

signals:
    /// A link was handed over to the system browser.
    void externalLinkOpened(const QUrl &url);
    /// A permission request has been answered (for the status bar / logs).
    void permissionResolved(const QString &permission, bool granted);
    /// A navigation attempt was rejected by the URL policy.
    void navigationBlocked(const QUrl &url);

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                  int lineNumber, const QString &sourceID) override;
    QWebEnginePage *createWindow(WebWindowType type) override;

private:
    void handlePermissionRequested(QWebEnginePermission permission);
    bool shouldGrant(QWebEnginePermission::PermissionType type) const;
    void handleCertificateError(const QWebEngineCertificateError &error);
    void handleRenderProcessTerminated(RenderProcessTerminationStatus status, int exitCode);
    bool openExternally(const QUrl &url);
    /// True when this origin is allowed to ask for permissions at all.
    bool isTrustedOrigin(const QUrl &origin) const;

    UrlPolicy m_policy = UrlPolicy::WhatsAppOnly;
    bool m_notificationsAllowed = true;
};

#endif // CORVO_WEBPAGE_H
