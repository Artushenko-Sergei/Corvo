#include "Settings.h"

#include "AppInfo.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// Keys -----------------------------------------------------------------------
const char *kWinSize        = "window/size";
const char *kWinPos         = "window/position";
const char *kWinMax         = "window/maximized";

const char *kTrayEnabled    = "tray/enabled";
const char *kCloseToTray    = "tray/closeToTray";
const char *kMinToTray      = "tray/minimizeToTray";
const char *kStartHidden    = "tray/startHidden";

const char *kNotifications  = "notifications/enabled";
const char *kLanguage       = "ui/language";
const char *kDetectedLang   = "ui/detectedLanguage";

const char *kProfilePath    = "profile/storagePath";
const char *kCachePath      = "profile/cachePath";
const char *kDownloadPath   = "profile/downloadPath";
const char *kSpellCheck     = "profile/spellCheckEnabled";
const char *kSpellLangs     = "profile/spellCheckLanguages";
const char *kZoom           = "profile/zoomPercent";
const char *kUserAgent      = "profile/userAgent";

QString genericPath(QStandardPaths::StandardLocation location, const QString &leaf)
{
    QString base = QStandardPaths::writableLocation(location);
    if (base.isEmpty())
        base = QDir::homePath();
    return QDir(base).filePath(leaf);
}

// Command for generated .desktop files. The running binary is used as is when it
// is installed; from a build directory an installed copy is preferred, so the
// entry keeps working after the build tree is gone.
QString launchCommand()
{
    const QString running = QCoreApplication::applicationFilePath();
    if (running.startsWith(QLatin1String("/usr/")) || running.startsWith(QLatin1String("/opt/")))
        return running;

    static const QStringList installed = {
        QStringLiteral("/opt/corvo/bin/Corvo"),
        QStringLiteral("/usr/bin/Corvo"),
        QStringLiteral("/usr/bin/corvo"),
        QStringLiteral("/usr/local/bin/Corvo"),
    };
    for (const QString &candidate : installed) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return running;
}

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(QDir(configPath()).filePath(QStringLiteral("Corvo.conf")),
                 QSettings::IniFormat)
{
    QDir().mkpath(configPath());
    QDir().mkpath(dataPath());
    qCInfo(logSettings) << "Configuration file:" << m_settings.fileName();
    qCInfo(logSettings) << "Data directory:" << dataPath();
}

// ---- Static locations ------------------------------------------------------

QString Settings::dataPath()
{
    return genericPath(QStandardPaths::GenericDataLocation, QStringLiteral("Corvo"));
}

QString Settings::configPath()
{
    return genericPath(QStandardPaths::GenericConfigLocation, QStringLiteral("Corvo"));
}

QString Settings::logPath()
{
    return QDir(dataPath()).filePath(QStringLiteral("logs"));
}

QString Settings::autostartFilePath()
{
    return genericPath(QStandardPaths::GenericConfigLocation,
                       QStringLiteral("autostart/Corvo.desktop"));
}

void Settings::migrateLegacyData()
{
    struct Move { QString from; QString to; const char *what; };
    const QString legacyData =
        genericPath(QStandardPaths::GenericDataLocation, QStringLiteral("QtWhatsApp"));
    const QString legacyConfigDir =
        genericPath(QStandardPaths::GenericConfigLocation, QStringLiteral("QtWhatsApp"));
    const QList<Move> moves{
        {legacyData, dataPath(), "data directory"},
        {legacyConfigDir, configPath(), "configuration directory"},
    };

    for (const Move &move : moves) {
        if (!QFileInfo::exists(move.from) || QFileInfo::exists(move.to))
            continue;
        QDir().mkpath(QFileInfo(move.to).absolutePath());
        if (QDir().rename(move.from, move.to)) {
            qCInfo(logSettings) << "Migrated" << move.what << move.from << "->" << move.to;
        } else {
            qCWarning(logSettings) << "Could not migrate" << move.what << move.from
                                   << "-> " << move.to;
        }
    }

    // The settings file inside the migrated directory still has the old name.
    const QString legacyConf = QDir(configPath()).filePath(QStringLiteral("QtWhatsApp.conf"));
    const QString conf = QDir(configPath()).filePath(QStringLiteral("Corvo.conf"));
    if (QFileInfo::exists(legacyConf) && !QFileInfo::exists(conf))
        QFile::rename(legacyConf, conf);

    // Same for the log file.
    const QString legacyLog = QDir(logPath()).filePath(QStringLiteral("QtWhatsApp.log"));
    const QString log = QDir(logPath()).filePath(QStringLiteral("Corvo.log"));
    if (QFileInfo::exists(legacyLog) && !QFileInfo::exists(log))
        QFile::rename(legacyLog, log);

    // An autostart entry from the old name would launch a binary that no longer
    // exists; drop it and let the user re-enable autostart.
    const QString legacyAutostart =
        genericPath(QStandardPaths::GenericConfigLocation,
                    QStringLiteral("autostart/QtWhatsApp.desktop"));
    if (QFileInfo::exists(legacyAutostart))
        QFile::remove(legacyAutostart);
}

QString Settings::settingsFilePath() const
{
    return m_settings.fileName();
}

// ---- Window geometry -------------------------------------------------------

QSize Settings::windowSize() const
{
    const QSize size = m_settings.value(kWinSize, QSize(1200, 800)).toSize();
    // Guard against a corrupted/degenerate stored value.
    if (!size.isValid() || size.width() < 480 || size.height() < 360)
        return QSize(1200, 800);
    return size;
}

void Settings::setWindowSize(const QSize &size)
{
    if (size.isValid())
        m_settings.setValue(kWinSize, size);
}

QPoint Settings::windowPosition() const
{
    return m_settings.value(kWinPos).toPoint();
}

bool Settings::hasWindowPosition() const
{
    return m_settings.value(kWinPos).canConvert<QPoint>()
           && m_settings.contains(QLatin1String(kWinPos));
}

void Settings::setWindowPosition(const QPoint &pos)
{
    m_settings.setValue(kWinPos, pos);
}

void Settings::clearWindowPosition()
{
    m_settings.remove(QLatin1String(kWinPos));
}

bool Settings::windowMaximized() const
{
    return m_settings.value(kWinMax, false).toBool();
}

void Settings::setWindowMaximized(bool maximized)
{
    m_settings.setValue(kWinMax, maximized);
}

// ---- Tray ------------------------------------------------------------------

bool Settings::trayEnabled() const
{
    return m_settings.value(kTrayEnabled, true).toBool();
}

void Settings::setTrayEnabled(bool enabled)
{
    if (trayEnabled() == enabled)
        return;
    m_settings.setValue(kTrayEnabled, enabled);
    emit trayEnabledChanged(enabled);
}

bool Settings::closeToTray() const
{
    return m_settings.value(kCloseToTray, true).toBool();
}

void Settings::setCloseToTray(bool enabled)
{
    m_settings.setValue(kCloseToTray, enabled);
}

bool Settings::minimizeToTray() const
{
    return m_settings.value(kMinToTray, false).toBool();
}

void Settings::setMinimizeToTray(bool enabled)
{
    m_settings.setValue(kMinToTray, enabled);
}

bool Settings::startHidden() const
{
    return m_settings.value(kStartHidden, false).toBool();
}

void Settings::setStartHidden(bool enabled)
{
    if (startHidden() == enabled)
        return;
    m_settings.setValue(kStartHidden, enabled);
    // The autostart entry carries --hidden, so it has to be rewritten.
    if (autostartEnabled())
        setAutostartEnabled(true);
}

// ---- Notifications ---------------------------------------------------------

bool Settings::notificationsEnabled() const
{
    return m_settings.value(kNotifications, true).toBool();
}

void Settings::setNotificationsEnabled(bool enabled)
{
    if (notificationsEnabled() == enabled)
        return;
    m_settings.setValue(kNotifications, enabled);
    emit notificationsEnabledChanged(enabled);
}

// ---- Interface language ----------------------------------------------------

QString Settings::language() const
{
    // Default: follow WhatsApp itself. Its language comes from the linked phone
    // and cannot be changed from here, so having the interface follow it keeps
    // the whole window consistent.
    const QString stored = m_settings.value(kLanguage, QStringLiteral("auto")).toString();
    return availableLanguages().contains(stored) ? stored : QStringLiteral("auto");
}

bool Settings::followsWebLanguage() const
{
    return language() == QLatin1String("auto");
}

QString Settings::effectiveLanguage() const
{
    return effectiveLanguageFor(language(), detectedWebLanguage());
}

QString Settings::effectiveLanguageFor(const QString &mode, const QString &detected)
{
    if (mode != QLatin1String("auto"))
        return mode;
    return detected.isEmpty() ? QStringLiteral("system") : detected;
}

QString Settings::detectedWebLanguage() const
{
    return m_settings.value(kDetectedLang).toString();
}

void Settings::setDetectedWebLanguage(const QString &code)
{
    if (code.isEmpty() || detectedWebLanguage() == code)
        return;
    m_settings.setValue(kDetectedLang, code);
    m_settings.sync();
    qCInfo(logSettings) << "WhatsApp interface language detected:" << code;
}

void Settings::setLanguage(const QString &code)
{
    const QString value = availableLanguages().contains(code) ? code
                                                             : QStringLiteral("auto");
    if (language() == value)
        return;
    m_settings.setValue(kLanguage, value);
    qCInfo(logSettings) << "UI language set to" << value;
}

QString Settings::storedLanguage()
{
    QSettings settings(QDir(configPath()).filePath(QStringLiteral("Corvo.conf")),
                       QSettings::IniFormat);
    QString mode = settings.value(kLanguage, QStringLiteral("auto")).toString();
    if (!availableLanguages().contains(mode))
        mode = QStringLiteral("auto");
    // In "auto" mode the best guess before the page has loaded is what WhatsApp
    // used last time; on a first run there is nothing better than the locale.
    return effectiveLanguageFor(mode, settings.value(kDetectedLang).toString());
}

QStringList Settings::availableLanguages()
{
    return {QStringLiteral("auto"), QStringLiteral("ru"), QStringLiteral("en"),
            QStringLiteral("de"), QStringLiteral("uk")};
}

QString Settings::languageDisplayName(const QString &code)
{
    if (code == QLatin1String("ru"))
        return QStringLiteral("Русский");
    if (code == QLatin1String("en"))
        return QStringLiteral("English");
    if (code == QLatin1String("de"))
        return QStringLiteral("Deutsch");
    if (code == QLatin1String("uk"))
        return QStringLiteral("Українська");
    return QObject::tr("Как в WhatsApp");
}

// ---- Desktop integration ---------------------------------------------------

QString Settings::userDesktopEntryPath()
{
    return genericPath(QStandardPaths::GenericDataLocation,
                       QStringLiteral("applications/" CORVO_APP_ID ".desktop"));
}

bool Settings::desktopEntryInstalled()
{
    return QFileInfo::exists(userDesktopEntryPath())
           || QFileInfo::exists(QStringLiteral("/usr/share/applications/" CORVO_APP_ID ".desktop"))
           || QFileInfo::exists(
               QStringLiteral("/usr/local/share/applications/" CORVO_APP_ID ".desktop"));
}

bool Settings::installDesktopEntry()
{
    const QString entry = userDesktopEntryPath();
    QDir().mkpath(QFileInfo(entry).absolutePath());

    // Icons first: the entry references them by name, so they must be findable
    // in the user's hicolor theme.
    const QString iconRoot = genericPath(QStandardPaths::GenericDataLocation,
                                         QStringLiteral("icons/hicolor"));
    bool iconsOk = true;
    const QList<int> sizes{16, 22, 24, 32, 48, 64, 128, 256};
    for (int size : sizes) {
        const QString dir = QStringLiteral("%1/%2x%2/apps").arg(iconRoot).arg(size);
        QDir().mkpath(dir);
        const QString target = dir + QStringLiteral("/" CORVO_APP_ID ".png");
        QFile::remove(target);
        if (!QFile::copy(QStringLiteral(":/icons/Corvo-%1.png").arg(size), target)) {
            iconsOk = false;
            qCWarning(logSettings) << "Cannot write icon" << target;
        }
    }
    const QString svgDir = iconRoot + QStringLiteral("/scalable/apps");
    QDir().mkpath(svgDir);
    const QString svgTarget = svgDir + QStringLiteral("/" CORVO_APP_ID ".svg");
    QFile::remove(svgTarget);
    QFile::copy(QStringLiteral(":/icons/Corvo.svg"), svgTarget);

    const QString exec = launchCommand();

    QFile file(entry);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qCWarning(logSettings) << "Cannot write desktop entry" << entry << file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << "[Desktop Entry]\n"
        // Desktop Entry Specification version, not the application version.
        << "Version=1.0\n"
        << "Type=Application\n"
        << "Name=Corvo\n"
        << "GenericName=WhatsApp Client\n"
        << "Comment=WhatsApp client for Linux\n"
        << "Exec=" << exec << " %U\n"
        << "Icon=" CORVO_APP_ID "\n"
        << "Terminal=false\n"
        << "Categories=Network;InstantMessaging;\n"
        << "StartupNotify=true\n"
        // Must match QGuiApplication::setDesktopFileName() / the Wayland app_id.
        << "StartupWMClass=Corvo\n";
    out.flush();
    file.close();

    qCInfo(logSettings) << "Desktop entry written to" << entry << "exec:" << exec
                        << "icons ok:" << iconsOk;
    return iconsOk;
}

// ---- Web profile -----------------------------------------------------------

QString Settings::profilePath() const
{
    const QString fallback = QDir(dataPath()).filePath(QStringLiteral("profile"));
    const QString stored = m_settings.value(kProfilePath, fallback).toString();
    return stored.isEmpty() ? fallback : stored;
}

void Settings::setProfilePath(const QString &path)
{
    m_settings.setValue(kProfilePath, path);
}

QString Settings::cachePath() const
{
    const QString fallback = QDir(dataPath()).filePath(QStringLiteral("cache"));
    const QString stored = m_settings.value(kCachePath, fallback).toString();
    return stored.isEmpty() ? fallback : stored;
}

void Settings::setCachePath(const QString &path)
{
    m_settings.setValue(kCachePath, path);
}

QString Settings::downloadPath() const
{
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (fallback.isEmpty())
        fallback = QDir(QDir::homePath()).filePath(QStringLiteral("Downloads"));
    const QString stored = m_settings.value(kDownloadPath, fallback).toString();
    return stored.isEmpty() ? fallback : stored;
}

void Settings::setDownloadPath(const QString &path)
{
    m_settings.setValue(kDownloadPath, path);
}

bool Settings::spellCheckEnabled() const
{
    return m_settings.value(kSpellCheck, false).toBool();
}

void Settings::setSpellCheckEnabled(bool enabled)
{
    m_settings.setValue(kSpellCheck, enabled);
}

QStringList Settings::spellCheckLanguages() const
{
    const QStringList stored = m_settings.value(kSpellLangs).toStringList();
    if (!stored.isEmpty())
        return stored;
    // Derive a sensible default from the current locale, e.g. "ru_RU" / "en_US".
    return QStringList{QLocale::system().name()};
}

void Settings::setSpellCheckLanguages(const QStringList &languages)
{
    m_settings.setValue(kSpellLangs, languages);
}

int Settings::zoomPercent() const
{
    const int value = m_settings.value(kZoom, 100).toInt();
    return qBound(50, value, 250);
}

void Settings::setZoomPercent(int percent)
{
    const int clamped = qBound(50, percent, 250);
    if (zoomPercent() == clamped)
        return;
    m_settings.setValue(kZoom, clamped);
    emit zoomPercentChanged(clamped);
}

QString Settings::customUserAgent() const
{
    return m_settings.value(kUserAgent, QString()).toString();
}

void Settings::setCustomUserAgent(const QString &userAgent)
{
    m_settings.setValue(kUserAgent, userAgent);
}

// ---- Autostart -------------------------------------------------------------

bool Settings::autostartSupported()
{
    // Every Flatpak sandbox has this file; its presence is the documented way to
    // detect one from inside.
    return !QFileInfo::exists(QStringLiteral("/.flatpak-info"));
}

bool Settings::autostartEnabled() const
{
    return QFileInfo::exists(autostartFilePath());
}

bool Settings::setAutostartEnabled(bool enabled)
{
    if (!autostartSupported()) {
        qCWarning(logSettings) << "Autostart is not available inside a sandbox; "
                                 "the Background portal is not implemented yet";
        return false;
    }

    const QString path = autostartFilePath();

    if (!enabled) {
        if (!QFileInfo::exists(path))
            return true;
        const bool removed = QFile::remove(path);
        qCInfo(logSettings) << "Autostart disabled, removed" << path << "->" << removed;
        return removed;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());

    const QString exec = launchCommand();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qCWarning(logSettings) << "Cannot write autostart file" << path << file.errorString();
        return false;
    }

    QTextStream out(&file);
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        // Desktop Entry Specification version, not the application version.
        << "Version=1.0\n"
        << "Name=Corvo\n"
        << "GenericName=WhatsApp Client\n"
        << "Comment=Native WhatsApp Web client for Linux\n"
        << "Exec=" << exec << (startHidden() ? " --hidden" : "") << "\n"
        << "Icon=" CORVO_APP_ID "\n"
        << "Terminal=false\n"
        << "Categories=Network;InstantMessaging;\n"
        << "StartupWMClass=Corvo\n"
        << "X-GNOME-Autostart-enabled=true\n";
    out.flush();
    file.close();

    qCInfo(logSettings) << "Autostart enabled:" << path << "exec:" << exec;
    return true;
}

void Settings::sync()
{
    m_settings.sync();
}
