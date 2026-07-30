// Settings.h - persistent application configuration (QSettings, INI format).
#ifndef CORVO_SETTINGS_H
#define CORVO_SETTINGS_H

#include <QObject>
#include <QPoint>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QStringList>

#include "AppInfo.h"

/**
 * Thin, strongly typed wrapper around QSettings.
 *
 * The file lives at ~/.config/Corvo/Corvo.conf and all user data
 * (web profile, cache, logs) below ~/.local/share/Corvo/ so the layout is
 * predictable regardless of the organisation name set on QCoreApplication.
 */
class Settings : public QObject
{
    Q_OBJECT

public:
    explicit Settings(QObject *parent = nullptr);

    // ---- Static locations -------------------------------------------------
    /// ~/.local/share/Corvo
    static QString dataPath();
    /// ~/.config/Corvo
    static QString configPath();
    /// ~/.local/share/Corvo/logs
    static QString logPath();
    /// ~/.config/autostart/Corvo.desktop
    static QString autostartFilePath();
    /**
     * Moves data written by the pre-rename versions (QtWhatsApp) to the current
     * locations. Runs before anything touches those paths and does nothing once
     * the new directories exist, so it is safe on every start.
     */
    static void migrateLegacyData();
    /// Absolute path of the QSettings file in use.
    QString settingsFilePath() const;

    // ---- Window geometry --------------------------------------------------
    QSize windowSize() const;
    void setWindowSize(const QSize &size);

    /// Stored top-left corner. Only meaningful when hasWindowPosition() is true;
    /// negative coordinates are legitimate on multi-monitor setups, so absence
    /// is tracked separately instead of using a (-1,-1) sentinel.
    QPoint windowPosition() const;
    bool hasWindowPosition() const;
    void setWindowPosition(const QPoint &pos);
    void clearWindowPosition();

    bool windowMaximized() const;
    void setWindowMaximized(bool maximized);

    // ---- Tray / window behaviour -----------------------------------------
    bool trayEnabled() const;
    void setTrayEnabled(bool enabled);

    bool closeToTray() const;
    void setCloseToTray(bool enabled);

    bool minimizeToTray() const;
    void setMinimizeToTray(bool enabled);

    bool startHidden() const;
    void setStartHidden(bool enabled);

    // ---- Notifications ---------------------------------------------------
    bool notificationsEnabled() const;
    void setNotificationsEnabled(bool enabled);

    // ---- Interface language ----------------------------------------------
    /**
     * Language mode of the interface: "auto" (follow the language WhatsApp Web
     * renders in - the default) or a fixed "ru", "en", "de", "uk".
     */
    QString language() const;
    /// True when the interface should follow the web page.
    bool followsWebLanguage() const;
    /**
     * Concrete language to use right now: the fixed choice, or - in "auto" mode -
     * the language WhatsApp used last time, or "system" on a first run.
     * Everything that needs a real language code goes through this, so the
     * translations, Accept-Language and Chromium's --lang cannot disagree.
     */
    QString effectiveLanguage() const;
    void setLanguage(const QString &code);
    /**
     * Reads the stored language directly from the settings file.
     * Usable before QApplication exists, which is when the Chromium --lang
     * switch has to be decided.
     */
    static QString storedLanguage();
    /// Shared logic of effectiveLanguage()/storedLanguage().
    static QString effectiveLanguageFor(const QString &mode, const QString &detected);
    /**
     * Language last reported by WhatsApp Web (<html lang>). Remembered so the
     * next start opens directly in the right language instead of flipping once
     * the page has loaded.
     */
    QString detectedWebLanguage() const;
    void setDetectedWebLanguage(const QString &code);

    /// Language codes this build ships translations for, in menu order.
    static QStringList availableLanguages();
    /// Human readable name of a language code, in that language itself.
    static QString languageDisplayName(const QString &code);

    // ---- Desktop integration ---------------------------------------------
    /// ~/.local/share/applications/Corvo.desktop
    static QString userDesktopEntryPath();
    /// True when a launcher entry exists for this user or system-wide.
    static bool desktopEntryInstalled();
    /**
     * Writes a launcher entry plus themed icons into ~/.local/share, so the
     * application has a proper name and icon in the menu, the dock and in
     * notifications even when it runs straight from the build directory.
     */
    bool installDesktopEntry();

    // ---- Web profile -----------------------------------------------------
    /// Directory holding cookies/localStorage/IndexedDB (the WhatsApp session).
    QString profilePath() const;
    void setProfilePath(const QString &path);

    /// Directory holding the HTTP disk cache.
    QString cachePath() const;
    void setCachePath(const QString &path);

    QString downloadPath() const;
    void setDownloadPath(const QString &path);

    bool spellCheckEnabled() const;
    void setSpellCheckEnabled(bool enabled);

    QStringList spellCheckLanguages() const;
    void setSpellCheckLanguages(const QStringList &languages);

    int zoomPercent() const;
    void setZoomPercent(int percent);

    /// Empty means "use the built-in Chrome-compatible string".
    QString customUserAgent() const;
    void setCustomUserAgent(const QString &userAgent);

    // ---- Autostart --------------------------------------------------------
    /**
     * False inside a Flatpak sandbox: writing ~/.config/autostart directly is
     * not allowed there (that is what org.freedesktop.portal.Background is for),
     * so the setting is presented as unavailable instead of silently failing.
     */
    static bool autostartSupported();
    /// True when ~/.config/autostart/Corvo.desktop exists.
    bool autostartEnabled() const;
    /// Writes or removes the autostart .desktop file. Returns false on I/O error.
    bool setAutostartEnabled(bool enabled);

    void sync();

signals:
    void notificationsEnabledChanged(bool enabled);
    void trayEnabledChanged(bool enabled);
    void zoomPercentChanged(int percent);

private:
    mutable QSettings m_settings;
};

#endif // CORVO_SETTINGS_H
