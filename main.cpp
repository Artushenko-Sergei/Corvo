// Corvo - native WhatsApp Web client for Linux (Qt 6 / Qt WebEngine).
//
// main() only bootstraps: Chromium flags, logging, single-instance guard and
// the main window. All behaviour lives in the classes under src/.

#include "src/AppInfo.h"
#include "src/Localization.h"
#include "src/Logger.h"
#include "src/MainWindow.h"
#include "src/Settings.h"
#include "src/WebProfile.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QSysInfo>

#include <cstdlib>

namespace {


/// Collects the values of a repeatable comma-separated switch out of a flag string.
QStringList switchValues(const QString &flags, const QString &name)
{
    QStringList values;
    const QString prefix = name + QLatin1Char('=');
    for (const QString &token : flags.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (token.startsWith(prefix))
            values += token.mid(prefix.size()).split(QLatin1Char(','), Qt::SkipEmptyParts);
    }
    return values;
}

/// Everything from `flags` except the given switches (they are re-emitted merged).
QStringList flagsWithout(const QString &flags, const QStringList &names)
{
    QStringList kept;
    for (const QString &token : flags.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        bool drop = false;
        for (const QString &name : names)
            drop = drop || token.startsWith(name + QLatin1Char('='));
        if (!drop)
            kept << token;
    }
    return kept;
}

/**
 * Chromium switches that must be in place *before* QApplication is created.
 *
 * Anything already present in QTWEBENGINE_CHROMIUM_FLAGS is preserved. Feature
 * lists are merged rather than appended, because Chromium keeps only the last
 * occurrence of --enable-features / --enable-blink-features, so a second switch
 * would silently drop the first one's values.
 */
void setupChromiumFlags()
{
    const QString existing = QString::fromLocal8Bit(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS"));

    QStringList features = switchValues(existing, QStringLiteral("--enable-features"));
    QStringList blinkFeatures =
        switchValues(existing, QStringLiteral("--enable-blink-features"));
    QStringList plain =
        flagsWithout(existing, {QStringLiteral("--enable-features"),
                                QStringLiteral("--enable-blink-features")});

    // Incoming-call ringtones and voice messages must be able to start on their
    // own, without a preceding user gesture in the page.
    plain << QStringLiteral("--autoplay-policy=no-user-gesture-required");

    // --- What WhatsApp Web needs for voice/video calls --------------------
    // Its calling engine is WebAssembly with threads, which requires
    // SharedArrayBuffer. Chromium only exposes SharedArrayBuffer to
    // cross-origin-isolated pages (COOP+COEP), and web.whatsapp.com is not
    // isolated, so without this switch the page reports
    // "Ваш браузер не поддерживает функцию звонков" and the call buttons do
    // nothing. This is the same escape hatch as Chrome's enterprise policy
    // SharedArrayBufferUnrestrictedAccessAllowed. It weakens a Spectre-era
    // mitigation, which is an acceptable trade-off for a single-site
    // application that only ever loads web.whatsapp.com.
    features << QStringLiteral("SharedArrayBuffer");
    // End-to-end encrypted calls transform encoded frames; WhatsApp uses the
    // current Encoded Transform API, which Qt's build does not enable by default
    // (the older createEncodedStreams() is present, RTCRtpScriptTransform is not).
    blinkFeatures << QStringLiteral("RTCRtpScriptTransform");

    // WebRTC needs a usable audio backend; PulseAudio/PipeWire is standard on
    // Debian 12 desktops and is what Chromium picks by default. On Wayland the
    // PipeWire capturer is required for screen sharing to work at all.
    if (qgetenv("XDG_SESSION_TYPE").toLower() == QByteArrayLiteral("wayland"))
        features << QStringLiteral("WebRTCPipeWireCapturer");

    // Language of the *page*, not of our own interface: WhatsApp Web reads
    // navigator.language, which Chromium derives from this switch. Without it the
    // engine follows the system locale, so switching the interface language left
    // the web content untouched. The setting has to be read straight from the
    // file because QApplication does not exist yet at this point.
    const QString pageLanguage =
        Localization::localeName(Localization::resolveLanguage(Settings::storedLanguage()));
    plain << QStringLiteral("--lang=%1").arg(pageLanguage);

    features.removeDuplicates();
    blinkFeatures.removeDuplicates();

    QStringList all = plain;
    if (!features.isEmpty())
        all << QStringLiteral("--enable-features=%1").arg(features.join(QLatin1Char(',')));
    if (!blinkFeatures.isEmpty()) {
        all << QStringLiteral("--enable-blink-features=%1")
                   .arg(blinkFeatures.join(QLatin1Char(',')));
    }

    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", all.join(QLatin1Char(' ')).toLocal8Bit());

    // Chromium's own logs (including WebRTC device enumeration) end up in our
    // log directory instead of polluting the terminal.
    const QString chromiumLog = QDir(Settings::logPath()).filePath(QStringLiteral("chromium.log"));
    QDir().mkpath(Settings::logPath());
    if (qgetenv("QTWEBENGINE_LOG_FILE").isEmpty())
        qputenv("QTWEBENGINE_LOG_FILE", chromiumLog.toLocal8Bit());
}

QString singleInstanceServerName()
{
    return QStringLiteral("Corvo-%1").arg(
        QString::fromLocal8Bit(qgetenv("USER").isEmpty() ? QByteArrayLiteral("user")
                                                         : qgetenv("USER")));
}

/// Returns true when another instance answered (this process should exit).
bool raiseExistingInstance()
{
    QLocalSocket socket;
    socket.connectToServer(singleInstanceServerName());
    if (!socket.waitForConnected(500))
        return false;

    socket.write(QByteArrayLiteral("raise"));
    socket.flush();
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // Data from the pre-rename versions lives under ~/.local/share/QtWhatsApp;
    // move it across before anything opens those paths, so the WhatsApp session
    // survives the rename instead of asking for a new QR code.
    Settings::migrateLegacyData();

    setupChromiumFlags();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Corvo"));
    QApplication::setApplicationDisplayName(QStringLiteral("Corvo"));
    // "1.0.0+12" - shown by --version and in the About dialog.
    QApplication::setApplicationVersion(QStringLiteral(CORVO_VERSION_FULL));
    // Base name of the desktop entry - deliberately WITHOUT the ".desktop"
    // suffix, as QGuiApplication::desktopFileName requires. Wayland uses it as
    // the app_id and the compositor looks up "<app_id>.desktop" to find the
    // application icon; with the suffix included it searched for
    // "Corvo.desktop.desktop" and showed no icon at all.
    QApplication::setDesktopFileName(QStringLiteral(CORVO_APP_ID));
    // Prefer the installed themed icon, fall back to the compiled-in one.
    QApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral(CORVO_APP_ID),
                         QIcon(QStringLiteral(":/icons/Corvo.png"))));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Corvo - нативный клиент WhatsApp Web для Linux"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption hiddenOption(
        QStringList{QStringLiteral("hidden"), QStringLiteral("tray")},
        QStringLiteral("Запустить свёрнутым в системный трей."));
    parser.addOption(hiddenOption);
    parser.process(app);

    Logger::install(Settings::logPath());
    qCInfo(logApp).noquote()
        << QStringLiteral("==== Corvo %1 (version %2, build %3) starting ====")
               .arg(QStringLiteral(CORVO_VERSION_FULL),
                    QStringLiteral(CORVO_VERSION))
               .arg(CORVO_BUILD);
    qCInfo(logApp) << "Qt" << qVersion() << "|" << QSysInfo::prettyProductName()
                   << "|" << QSysInfo::currentCpuArchitecture();
    qCInfo(logApp) << "Session type:" << qgetenv("XDG_SESSION_TYPE")
                   << "| Qt platform:" << QApplication::platformName();
    qCInfo(logApp) << "Chromium flags:" << qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    qCInfo(logApp) << "Log file:" << Logger::logFilePath();

    // A second instance would fight over the Chromium profile lock, so hand the
    // request over to the running one and exit.
    if (raiseExistingInstance()) {
        qCInfo(logApp) << "Another instance is already running, raising its window";
        Logger::shutdown();
        return 0;
    }
    QLocalServer::removeServer(singleInstanceServerName());
    QLocalServer instanceServer;
    if (!instanceServer.listen(singleInstanceServerName()))
        qCWarning(logApp) << "Single-instance server failed:" << instanceServer.errorString();

    // Declaration order matters: locals are destroyed in reverse, so the window
    // (and every page below it) goes away before the profile it uses. Qt
    // WebEngine requires exactly that - a profile outliving all of its pages.
    Settings settings;
    // Translators must be installed before any widget is created.
    Localization::install(&app, settings.effectiveLanguage());

    WebProfile profile(&settings);
    MainWindow window(&settings, &profile);

    QObject::connect(&instanceServer, &QLocalServer::newConnection, &window, [&]() {
        QLocalSocket *client = instanceServer.nextPendingConnection();
        if (!client)
            return;
        QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        window.showAndActivate();
    });

    // Hidden start is for autostart at login, which passes --hidden; launching
    // the application by hand always shows the window. Without a tray icon there
    // would be no way to bring it back, so the window is shown regardless.
    if (parser.isSet(hiddenOption) && settings.trayEnabled()) {
        qCInfo(logApp) << "Starting hidden in the tray";
    } else {
        window.show();
    }

    const int code = QApplication::exec();
    qCInfo(logApp) << "==== Corvo exiting with code" << code << "====";
    Logger::shutdown();
    return code;
}
