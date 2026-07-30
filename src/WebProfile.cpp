#include "WebProfile.h"

#include "Localization.h"
#include "Logger.h"
#include "Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QWebEngineDownloadRequest>
#include <QWebEngineSettings>

namespace {
constexpr int kHttpCacheMaxBytes = 250 * 1024 * 1024; // 250 MiB
}

WebProfile::WebProfile(Settings *settings, QObject *parent)
    // A *named* profile is what makes the profile off-the-record == false, i.e.
    // the precondition for any persistent cookie/localStorage storage at all.
    : QWebEngineProfile(QStringLiteral("Corvo"), parent)
    , m_settings(settings)
    , m_qtUserAgent(httpUserAgent())
{
    qCInfo(logWeb) << "Qt default user agent:" << m_qtUserAgent;

    // Paths must be set before Chromium opens the storage partition, i.e. here
    // and never again - see applyRuntimeConfiguration().
    configurePaths();
    configureWebSettings();
    applyRuntimeConfiguration();

    connect(this, &QWebEngineProfile::downloadRequested,
            this, &WebProfile::handleDownloadRequested);

    qCInfo(logWeb) << "Profile created:" << storageName()
                   << "off-the-record:" << isOffTheRecord();
}

void WebProfile::applyRuntimeConfiguration()
{
    const QString ua = m_settings && !m_settings->customUserAgent().isEmpty()
                           ? m_settings->customUserAgent()
                           : sanitizeUserAgent(m_qtUserAgent);
    setHttpUserAgent(ua);
    qCInfo(logWeb) << "User agent:" << ua;

    // WhatsApp Web picks its own language from this header (and from
    // navigator.language, set through Chromium's --lang in main()), so it has to
    // follow the interface language rather than the system locale.
    const QString uiLanguage = Localization::resolveLanguage(
        m_settings ? m_settings->effectiveLanguage() : QString());
    const QString accept = Localization::acceptLanguage(uiLanguage);
    setHttpAcceptLanguage(accept);
    qCInfo(logWeb) << "Accept-Language:" << accept;

    if (m_settings) {
        const QString downloads = m_settings->downloadPath();
        QDir().mkpath(downloads);
        setDownloadPath(downloads);

        setSpellCheckEnabled(m_settings->spellCheckEnabled());
        setSpellCheckLanguages(m_settings->spellCheckLanguages());
        qCInfo(logWeb) << "Spell checking:" << isSpellCheckEnabled()
                       << spellCheckLanguages();
    }
}

void WebProfile::configurePaths()
{
    const QString storage = m_settings ? m_settings->profilePath()
                                       : QDir(Settings::dataPath()).filePath(QStringLiteral("profile"));
    const QString cache = m_settings ? m_settings->cachePath()
                                     : QDir(Settings::dataPath()).filePath(QStringLiteral("cache"));
    QDir().mkpath(storage);
    QDir().mkpath(cache);

    setPersistentStoragePath(storage);
    setCachePath(cache);

    // Cookies must survive process exit - otherwise the QR code comes back.
    setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    setHttpCacheMaximumSize(kHttpCacheMaxBytes);

    // Remember permission decisions across restarts. This only covers the types
    // Qt considers persistent (notifications, geolocation, ...);
    // camera/microphone requests are transient by design
    // (QWebEnginePermission::isPersistent() == false) and are re-granted by
    // WebPage on every session anyway.
    setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::StoreOnDisk);
    qCInfo(logWeb) << "Persistent permissions: StoreOnDisk";

    qCInfo(logWeb) << "Persistent storage:" << persistentStoragePath();
    qCInfo(logWeb) << "Disk cache:" << cachePath();
    qCInfo(logWeb) << "Cookies policy: ForcePersistentCookies";
}

void WebProfile::configureWebSettings()
{
    QWebEngineSettings *s = settings();
    if (!s) {
        qCWarning(logWeb) << "QWebEngineSettings unavailable for this profile";
        return;
    }

    // --- Required by WhatsApp Web ----------------------------------------
    s->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    s->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    s->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
    s->setAttribute(QWebEngineSettings::JavascriptCanPaste, true);
    s->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    s->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, true);   // screen sharing in calls
    s->setAttribute(QWebEngineSettings::WebGLEnabled, true);           // stickers / canvas effects
    s->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    s->setAttribute(QWebEngineSettings::AutoLoadImages, true);
    s->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    s->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, true);
    s->setAttribute(QWebEngineSettings::ErrorPageEnabled, true);
    s->setAttribute(QWebEngineSettings::ShowScrollBars, true);
    // Incoming-call ringtones and voice notes must play without a click first.
    s->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);

    // --- Security hardening (section 8) ----------------------------------
    // No JavaScript access to the local filesystem, ever.
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    s->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
    s->setAttribute(QWebEngineSettings::AllowGeolocationOnInsecureOrigins, false);
    s->setAttribute(QWebEngineSettings::AllowWindowActivationFromJavaScript, false);
    s->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
    s->setAttribute(QWebEngineSettings::XSSAuditingEnabled, false);
    s->setAttribute(QWebEngineSettings::NavigateOnDropEnabled, false);

    // --- Features WhatsApp does not use ----------------------------------
    s->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    s->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);
    s->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, false);
    s->setAttribute(QWebEngineSettings::TouchIconsEnabled, false);
    s->setAttribute(QWebEngineSettings::PrintElementBackgrounds, false);
    s->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, true);
    // Do not leak the list of local network interfaces through WebRTC ICE;
    // the TURN/STUN path still works, so calls are unaffected.
    s->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);

    // Popups are intercepted in WebPage::createWindow() and routed either to a
    // popup window (whatsapp.com) or to the system browser (everything else).
    s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);

    qCInfo(logWeb) << "WebEngine attributes applied "
                      "(file access disabled, plugins disabled, WebRTC restricted)";
}

QString WebProfile::fallbackUserAgent()
{
    return QStringLiteral(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/120.0.0.0 Safari/537.36");
}

QString WebProfile::sanitizeUserAgent(const QString &qtUserAgent)
{
    // Oldest Chromium major version WhatsApp Web still serves the app to.
    constexpr int kMinimumChromeMajor = 110;

    if (qtUserAgent.isEmpty())
        return fallbackUserAgent();

    QString ua = qtUserAgent;
    // "... (KHTML, like Gecko) QtWebEngine/6.11.1 Chrome/136.0.0.0 Safari/537.36"
    static const QRegularExpression qtToken(QStringLiteral("QtWebEngine/\\S+\\s*"));
    ua.remove(qtToken).replace(QStringLiteral("  "), QStringLiteral(" "));

    static const QRegularExpression chromeVersion(QStringLiteral("Chrome/(\\d+)"));
    const QRegularExpressionMatch match = chromeVersion.match(ua);
    if (!match.hasMatch()) {
        qCWarning(logWeb) << "No Chrome token in Qt's user agent, using the fallback";
        return fallbackUserAgent();
    }

    const int major = match.captured(1).toInt();
    if (major < kMinimumChromeMajor) {
        qCInfo(logWeb) << "Engine reports Chrome/" << major
                       << "which WhatsApp Web rejects; using the fallback user agent";
        return fallbackUserAgent();
    }
    return ua.trimmed();
}

void WebProfile::handleDownloadRequested(QWebEngineDownloadRequest *download)
{
    if (!download)
        return;

    const QString directory = m_settings ? m_settings->downloadPath() : downloadPath();
    QDir().mkpath(directory);
    download->setDownloadDirectory(directory);

    const QString fileName = download->downloadFileName();
    qCInfo(logWeb) << "Download accepted:" << fileName << "->" << directory;

    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, this, [this, download]() {
        const bool ok = download->state() == QWebEngineDownloadRequest::DownloadCompleted;
        qCInfo(logWeb) << "Download finished:" << download->downloadFileName()
                       << "state:" << download->state();
        emit downloadFinished(download->downloadFileName(), ok);
    });

    download->accept();
    emit downloadStarted(fileName, directory);
}
