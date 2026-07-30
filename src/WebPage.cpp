#include "WebPage.h"

#include "Logger.h"

#include <QDesktopServices>
#include <QMetaEnum>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineCertificateError>
#include <QWebEngineLoadingInfo>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWidget>

namespace {

constexpr char kWhatsAppHost[] = "web.whatsapp.com";
/// A popup that never starts loading is closed after this long (see PopupWindow).
constexpr int kPopupWatchdogMs = 30000;

/**
 * Container for pages opened via window.open() from web.whatsapp.com
 * (group-call windows, media viewers).
 *
 * It is parented to the main window - so it can never outlive the profile - but
 * carries Qt::Window so it still behaves as a separate top-level window. It is
 * only shown once the child page actually starts loading an allowed URL; popups
 * that turn out to be plain external links, or that never navigate at all,
 * close themselves instead of lingering invisibly.
 */
class PopupWindow : public QWidget
{
public:
    PopupWindow(QWebEngineProfile *profile, WebPage::UrlPolicy policy, QWidget *parent)
        : QWidget(parent, Qt::Window)
        , m_view(new QWebEngineView(this))
    {
        setWindowTitle(QStringLiteral("Corvo"));
        setWindowIcon(QIcon(QStringLiteral(":/icons/Corvo.png")));
        setAttribute(Qt::WA_DeleteOnClose);
        resize(900, 640);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_view);

        m_page = new WebPage(profile, policy, m_view);
        m_view->setPage(m_page);

        connect(m_page, &QWebEnginePage::loadStarted, this, [this]() {
            m_navigated = true;
            if (!isVisible()) {
                show();
                raise();
                activateWindow();
            }
        });
        connect(m_page, &WebPage::externalLinkOpened, this, &QWidget::close);
        connect(m_page, &WebPage::navigationBlocked, this, &QWidget::close);
        connect(m_page, &QWebEnginePage::windowCloseRequested, this, &QWidget::close);
        connect(m_page, &QWebEnginePage::titleChanged, this, [this](const QString &title) {
            setWindowTitle(title.isEmpty() ? QStringLiteral("Corvo") : title);
        });
        connect(m_page, &QWebEnginePage::geometryChangeRequested, this,
                [this](const QRect &rect) {
                    if (rect.width() > 200 && rect.height() > 200)
                        resize(rect.size());
                });

        // Chromium can create a window that is never navigated (window.open()
        // followed by document.write()). Without this it would stay alive and
        // invisible for the rest of the session.
        QTimer::singleShot(kPopupWatchdogMs, this, [this]() {
            if (!m_navigated && !isVisible()) {
                qCInfo(logWeb) << "Closing popup that never started loading";
                close();
            }
        });
    }

    WebPage *page() const { return m_page; }

private:
    QWebEngineView *m_view = nullptr;
    WebPage *m_page = nullptr;
    bool m_navigated = false;
};

const char *consoleLevelName(QWebEnginePage::JavaScriptConsoleMessageLevel level)
{
    switch (level) {
    case QWebEnginePage::InfoMessageLevel:    return "info";
    case QWebEnginePage::WarningMessageLevel: return "warning";
    case QWebEnginePage::ErrorMessageLevel:   return "error";
    }
    return "log";
}

} // namespace

WebPage::WebPage(QWebEngineProfile *profile, UrlPolicy policy, QObject *parent)
    : QWebEnginePage(profile, parent)
    , m_policy(policy)
{
    // The request object carries grant()/deny() and, depending on the profile's
    // persistent-permissions policy, is remembered across restarts.
    connect(this, &QWebEnginePage::permissionRequested,
            this, &WebPage::handlePermissionRequested);
    connect(this, &QWebEnginePage::certificateError,
            this, &WebPage::handleCertificateError);
    connect(this, &QWebEnginePage::renderProcessTerminated,
            this, &WebPage::handleRenderProcessTerminated);

    connect(this, &QWebEnginePage::loadingChanged, this,
            [](const QWebEngineLoadingInfo &info) {
                if (info.status() == QWebEngineLoadingInfo::LoadFailedStatus) {
                    qCWarning(logWeb) << "Load failed:" << info.url().toString()
                                      << "error:" << info.errorString()
                                      << "code:" << info.errorCode();
                }
            });

    qCInfo(logWeb) << "WebPage created, policy:"
                   << (policy == UrlPolicy::WhatsAppOnly ? "WhatsAppOnly" : "LocalDiagnostics");
}

// ---------------------------------------------------------------------------
// Permissions (sections 5 and 6)
// ---------------------------------------------------------------------------

bool WebPage::isTrustedOrigin(const QUrl &origin) const
{
    // The bundled diagnostics page (qrc:) may ask for camera/microphone too.
    if (m_policy == UrlPolicy::LocalDiagnostics)
        return origin.scheme() == QLatin1String("qrc") || origin.isEmpty();
    return isWhatsAppHost(origin);
}

QString WebPage::permissionName(QWebEnginePermission::PermissionType type)
{
    const QMetaEnum meta = QMetaEnum::fromType<QWebEnginePermission::PermissionType>();
    const char *key = meta.valueToKey(static_cast<int>(type));
    return key ? QString::fromLatin1(key)
               : QStringLiteral("Unknown(%1)").arg(static_cast<int>(type));
}

bool WebPage::shouldGrant(QWebEnginePermission::PermissionType type) const
{
    using PermissionType = QWebEnginePermission::PermissionType;
    switch (type) {
    case PermissionType::MediaAudioCapture:       // microphone
    case PermissionType::MediaVideoCapture:       // camera
    case PermissionType::MediaAudioVideoCapture:  // video call
    case PermissionType::DesktopVideoCapture:     // screen sharing
    case PermissionType::DesktopAudioVideoCapture:
        return true;
    case PermissionType::Notifications:
        return m_notificationsAllowed;
    case PermissionType::ClipboardReadWrite:
        // Needed to paste images/text into a chat.
        return true;
    case PermissionType::Geolocation:
    case PermissionType::MouseLock:
    case PermissionType::LocalFontsAccess:
    case PermissionType::Unsupported:
        break;
    }
    return false;
}

void WebPage::handlePermissionRequested(QWebEnginePermission permission)
{
    const QString name = permissionName(permission.permissionType());
    const QUrl origin = permission.origin();

    qCInfo(logPermission).noquote() << "Permission request:\n " << name
                                   << "\n  origin:" << origin.toString()
                                   << "\n  current state:"
                                   << static_cast<int>(permission.state())
                                   << "\n  persistent:"
                                   << QWebEnginePermission::isPersistent(
                                          permission.permissionType());

    if (!permission.isValid()) {
        qCWarning(logPermission) << "Ignoring invalid permission request for" << name;
        return;
    }

    if (!isTrustedOrigin(origin)) {
        qCWarning(logPermission) << "Denied" << name << "for foreign origin"
                                 << origin.toString();
        permission.deny();
        emit permissionResolved(name, false);
        return;
    }

    const bool grant = shouldGrant(permission.permissionType());
    if (grant)
        permission.grant();
    else
        permission.deny();

    qCInfo(logPermission).noquote()
        << (grant ? "Granted:" : "Denied: ") << name << "for" << origin.toString();
    emit permissionResolved(name, grant);
}


// ---------------------------------------------------------------------------
// URL policy (section 8)
// ---------------------------------------------------------------------------

bool WebPage::isWhatsAppMainUrl(const QUrl &url)
{
    return url.scheme() == QLatin1String("https")
           && url.host().compare(QLatin1String(kWhatsAppHost), Qt::CaseInsensitive) == 0;
}

bool WebPage::isWhatsAppHost(const QUrl &url)
{
    const QString host = url.host().toLower();
    return host == QLatin1String("whatsapp.com")
           || host == QLatin1String("whatsapp.net")
           || host.endsWith(QLatin1String(".whatsapp.com"))
           || host.endsWith(QLatin1String(".whatsapp.net"));
}

bool WebPage::openExternally(const QUrl &url)
{
    qCInfo(logWeb) << "Opening in system browser:" << url.toString();
    const bool ok = QDesktopServices::openUrl(url);
    if (!ok)
        qCWarning(logWeb) << "QDesktopServices::openUrl() failed for" << url.toString();
    emit externalLinkOpened(url);
    return ok;
}

bool WebPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame)
{
    const QString scheme = url.scheme().toLower();

    if (m_policy == UrlPolicy::LocalDiagnostics) {
        if (scheme == QLatin1String("qrc") || scheme == QLatin1String("about")
            || scheme == QLatin1String("data")) {
            return true;
        }
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
            openExternally(url);
            return false;
        }
        qCWarning(logWeb) << "Diagnostics page blocked navigation to" << url.toString();
        emit navigationBlocked(url);
        return false;
    }

    // Internal schemes used by Chromium itself.
    if (scheme == QLatin1String("about") || scheme == QLatin1String("blob"))
        return true;
    if (scheme == QLatin1String("data") && !isMainFrame)
        return true;

    // Hard block on local file access, whatever the frame.
    if (scheme == QLatin1String("file") || scheme == QLatin1String("qrc")
        || scheme == QLatin1String("data")) {
        qCWarning(logWeb) << "Blocked local/inline navigation:" << url.toString()
                          << "type:" << type << "mainFrame:" << isMainFrame;
        emit navigationBlocked(url);
        return false;
    }

    if (scheme == QLatin1String("https") || scheme == QLatin1String("http")) {
        if (isMainFrame) {
            if (isWhatsAppMainUrl(url))
                return true;
            // Any other page - including other whatsapp.com hosts such as
            // faq.whatsapp.com - belongs in the user's browser.
            openExternally(url);
            return false;
        }
        // Sub-frames: WhatsApp's own infrastructure only.
        if (isWhatsAppHost(url))
            return true;
        qCWarning(logWeb) << "Blocked sub-frame navigation:" << url.toString();
        emit navigationBlocked(url);
        return false;
    }

    // mailto:, tel:, whatsapp: ... - hand over to the desktop.
    if (scheme == QLatin1String("mailto") || scheme == QLatin1String("tel")
        || scheme == QLatin1String("whatsapp")) {
        openExternally(url);
        return false;
    }

    qCWarning(logWeb) << "Blocked navigation, unsupported scheme:" << url.toString();
    emit navigationBlocked(url);
    return false;
}

QWebEnginePage *WebPage::createWindow(WebWindowType type)
{
    qCInfo(logWeb) << "createWindow() requested, type:" << type;

    switch (type) {
    case QWebEnginePage::WebBrowserTab:
    case QWebEnginePage::WebBrowserWindow:
    case QWebEnginePage::WebDialog: {
        // Parent the popup to the window that owns this page, so that it is
        // destroyed together with it - and therefore always before the profile.
        QWidget *owner = nullptr;
        if (QWebEngineView *view = QWebEngineView::forPage(this))
            owner = view->window();
        auto *popup = new PopupWindow(profile(), m_policy, owner);
        return popup->page();
    }
    case QWebEnginePage::WebBrowserBackgroundTab:
        return nullptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Diagnostics (section 14)
// ---------------------------------------------------------------------------

void WebPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message,
                                       int lineNumber, const QString &sourceID)
{
    const QString text = QStringLiteral("JS [%1] %2 (%3:%4)")
                             .arg(QLatin1String(consoleLevelName(level)), message,
                                  sourceID.isEmpty() ? QStringLiteral("<inline>") : sourceID)
                             .arg(lineNumber);
    if (level == QWebEnginePage::ErrorMessageLevel)
        qCWarning(logWeb).noquote() << text;
    else
        qCDebug(logWeb).noquote() << text;
}

void WebPage::handleCertificateError(const QWebEngineCertificateError &error)
{
    // Rejected by default: we deliberately never override TLS errors, because
    // the only origin this application talks to is web.whatsapp.com.
    qCCritical(logWeb) << "Certificate error for" << error.url().toString() << ":"
                       << error.description() << "overridable:" << error.isOverridable()
                       << "-> rejected";
}

void WebPage::handleRenderProcessTerminated(RenderProcessTerminationStatus status, int exitCode)
{
    qCCritical(logWeb) << "Render process terminated, status:" << status
                       << "exit code:" << exitCode;
}
