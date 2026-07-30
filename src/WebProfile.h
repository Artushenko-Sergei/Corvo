// WebProfile.h - configured QWebEngineProfile (persistent session, cache, policy).
#ifndef CORVO_WEBPROFILE_H
#define CORVO_WEBPROFILE_H

#include <QWebEngineProfile>

class Settings;
class QWebEngineDownloadRequest;

/**
 * The single persistent web profile used by the application.
 *
 * Everything that makes the WhatsApp session survive a restart lives here:
 * a persistent storage path (cookies, localStorage, IndexedDB), a disk HTTP
 * cache and ForcePersistentCookies. It also narrows down the Chromium feature
 * set to what WhatsApp Web actually needs.
 *
 * Ownership: the profile is created in main() and therefore outlives every
 * window and page in the application, which is what Qt WebEngine requires
 * ("Release of profile requested but WebEnginePage still not deleted").
 */
class WebProfile : public QWebEngineProfile
{
    Q_OBJECT

public:
    explicit WebProfile(Settings *settings, QObject *parent = nullptr);

    /**
     * Re-applies the settings that Chromium accepts on a live profile:
     * user agent, spell checking and the download directory.
     *
     * Storage and cache paths are deliberately NOT touched here. Chromium has
     * already opened the storage partition by then, so changing the path only
     * updates what the profile reports while the old partition stays in use
     * (verified: localStorage still returns values from the previous path).
     * Those two settings are applied once, in the constructor, and need an
     * application restart to change.
     */
    void applyRuntimeConfiguration();

    /**
     * Turns Qt's own user agent into one WhatsApp Web accepts.
     *
     * Qt appends a "QtWebEngine/<version>" token, which web.whatsapp.com uses to
     * serve the "unsupported browser" page, so that token is removed. If the
     * underlying Chromium is too old to be accepted at all (Qt 6.4 reports
     * Chrome/102), the modern fallback string below is used instead. Deriving the
     * value keeps the real engine version on newer Qt (6.11 reports Chrome/13x)
     * instead of pinning an ever-older number.
     */
    static QString sanitizeUserAgent(const QString &qtUserAgent);

    /// Hardcoded modern Chrome UA, used when the engine's own version is too old.
    static QString fallbackUserAgent();

signals:
    /// Emitted for every started download so the UI can report it.
    void downloadStarted(const QString &fileName, const QString &directory);
    void downloadFinished(const QString &fileName, bool success);

private:
    void configurePaths();
    void configureWebSettings();
    void handleDownloadRequested(QWebEngineDownloadRequest *download);

    Settings *m_settings = nullptr;
    /// Qt's untouched user agent, captured before we override it.
    QString m_qtUserAgent;
};

#endif // CORVO_WEBPROFILE_H
