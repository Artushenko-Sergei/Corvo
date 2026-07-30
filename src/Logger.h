// Logger.h - application-wide logging categories and file logging.
#ifndef CORVO_LOGGER_H
#define CORVO_LOGGER_H

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(logApp)
Q_DECLARE_LOGGING_CATEGORY(logWeb)
Q_DECLARE_LOGGING_CATEGORY(logPermission)
Q_DECLARE_LOGGING_CATEGORY(logNotify)
Q_DECLARE_LOGGING_CATEGORY(logTray)
Q_DECLARE_LOGGING_CATEGORY(logSettings)
Q_DECLARE_LOGGING_CATEGORY(logMedia)

namespace Logger {

/// Installs a message handler that mirrors every qDebug()/qCWarning() line to
/// <logDir>/Corvo.log in addition to stderr. Rotates the file once it
/// exceeds ~5 MiB (a single .1 generation is kept).
void install(const QString &logDir);

/// Flushes and closes the log file. Called from main() on shutdown.
void shutdown();

/// Absolute path of the active log file (empty when install() was not called).
QString logFilePath();

} // namespace Logger

#endif // CORVO_LOGGER_H
