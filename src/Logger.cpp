#include "Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>

Q_LOGGING_CATEGORY(logApp, "corvo.app")
Q_LOGGING_CATEGORY(logWeb, "corvo.web")
Q_LOGGING_CATEGORY(logPermission, "corvo.permission")
Q_LOGGING_CATEGORY(logNotify, "corvo.notify")
Q_LOGGING_CATEGORY(logTray, "corvo.tray")
Q_LOGGING_CATEGORY(logSettings, "corvo.settings")
Q_LOGGING_CATEGORY(logMedia, "corvo.media")

namespace {

constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024;

QMutex g_mutex;
QFile g_file;
QString g_path;
QtMessageHandler g_previousHandler = nullptr;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "debug";
    case QtInfoMsg:     return "info";
    case QtWarningMsg:  return "warning";
    case QtCriticalMsg: return "critical";
    case QtFatalMsg:    return "fatal";
    }
    return "unknown";
}

void rotateIfNeeded(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || info.size() < kMaxLogBytes)
        return;
    const QString rotated = path + QLatin1String(".1");
    QFile::remove(rotated);
    QFile::rename(path, rotated);
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // Keep the default (stderr) output so `journalctl`/terminal runs still work.
    if (g_previousHandler)
        g_previousHandler(type, context, message);

    QMutexLocker locker(&g_mutex);
    if (!g_file.isOpen())
        return;

    const QString category = context.category ? QString::fromLatin1(context.category)
                                              : QStringLiteral("default");
    QString line = QStringLiteral("%1 [%2] %3: %4")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                            QLatin1String(levelName(type)), category, message);
    if (context.file && type != QtDebugMsg && type != QtInfoMsg)
        line += QStringLiteral(" (%1:%2)").arg(QLatin1String(context.file)).arg(context.line);

    QTextStream out(&g_file);
    out << line << Qt::endl;
    g_file.flush();
}

} // namespace

namespace Logger {

void install(const QString &logDir)
{
    QDir().mkpath(logDir);

    const QString path = QDir(logDir).filePath(QStringLiteral("Corvo.log"));
    rotateIfNeeded(path);

    QMutexLocker locker(&g_mutex);
    g_path = path;
    g_file.setFileName(path);
    if (!g_file.open(QIODevice::Append | QIODevice::Text | QIODevice::WriteOnly)) {
        std::fprintf(stderr, "Corvo: cannot open log file %s\n", qPrintable(path));
        g_path.clear();
        return;
    }
    locker.unlock();

    g_previousHandler = qInstallMessageHandler(messageHandler);

    // Categories are all enabled by default; QT_LOGGING_RULES still overrides this.
    QLoggingCategory::setFilterRules(QStringLiteral("corvo.*=true"));
}

void shutdown()
{
    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;

    QMutexLocker locker(&g_mutex);
    if (g_file.isOpen()) {
        g_file.flush();
        g_file.close();
    }
}

QString logFilePath()
{
    QMutexLocker locker(&g_mutex);
    return g_path;
}

} // namespace Logger
