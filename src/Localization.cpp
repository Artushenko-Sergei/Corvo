#include "Localization.h"

#include "Logger.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QStringList>
#include <QTranslator>

namespace {

/// Languages with a shipped .qm file. Russian is the source language.
const QStringList &translatedLanguages()
{
    static const QStringList languages{QStringLiteral("en"), QStringLiteral("de"),
                                       QStringLiteral("uk")};
    return languages;
}

constexpr char kSourceLanguage[] = "ru";

// Translators currently installed, so they can be removed on a language switch.
QTranslator *g_appTranslator = nullptr;
QTranslator *g_qtTranslator = nullptr;
QString g_currentLanguage;

} // namespace

namespace Localization {

QString resolveLanguage(const QString &configured)
{
    const auto isSupported = [](const QString &code) {
        return code == QLatin1String(kSourceLanguage) || translatedLanguages().contains(code);
    };

    if (!configured.isEmpty() && configured != QLatin1String("system"))
        return isSupported(configured) ? configured : QStringLiteral("en");

    // "system": walk the user's ordered preference list (this honours LANGUAGE,
    // LC_ALL and LANG, unlike looking at a single locale name) and take the
    // first language we actually ship.
    const QStringList preferred = QLocale::system().uiLanguages();
    for (const QString &entry : preferred) {
        const QString code =
            entry.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
        if (isSupported(code))
            return code;
    }
    // Nothing matched: English travels further than the Russian source strings.
    return QStringLiteral("en");
}

QString localeName(const QString &code)
{
    if (code == QLatin1String("ru"))
        return QStringLiteral("ru-RU");
    if (code == QLatin1String("de"))
        return QStringLiteral("de-DE");
    if (code == QLatin1String("uk"))
        return QStringLiteral("uk-UA");
    if (code == QLatin1String("en"))
        return QStringLiteral("en-US");
    return code;
}

QString acceptLanguage(const QString &code)
{
    // "de-DE,de;q=0.9,en-US;q=0.8,en;q=0.7" - the chosen language first, English
    // as the fallback every site understands.
    const QString full = localeName(code);
    QString value = QStringLiteral("%1,%2;q=0.9").arg(full, code);
    if (code != QLatin1String("en"))
        value += QStringLiteral(",en-US;q=0.8,en;q=0.7");
    return value;
}

QString matchSupported(const QString &tag)
{
    const QString code =
        tag.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
    if (code.isEmpty())
        return QString();
    if (code == QLatin1String(kSourceLanguage) || translatedLanguages().contains(code))
        return code;
    return QString();
}

QString currentLanguage()
{
    return g_currentLanguage;
}

bool apply(QCoreApplication *app, const QString &code)
{
    if (code.isEmpty() || code == g_currentLanguage)
        return false;

    // Removing and installing translators makes Qt post QEvent::LanguageChange
    // to every widget, which is what triggers retranslation of the UI.
    if (g_appTranslator) {
        QCoreApplication::removeTranslator(g_appTranslator);
        delete g_appTranslator;
        g_appTranslator = nullptr;
    }
    if (g_qtTranslator) {
        QCoreApplication::removeTranslator(g_qtTranslator);
        delete g_qtTranslator;
        g_qtTranslator = nullptr;
    }

    QLocale::setDefault(QLocale(localeName(code)));
    g_currentLanguage = code;

    if (code == QLatin1String(kSourceLanguage)) {
        qCInfo(logApp) << "Interface language switched to the source language" << code;
        return true;
    }

    auto *appTranslator = new QTranslator(app);
    const QString resource = QStringLiteral(":/i18n/Corvo_%1.qm").arg(code);
    if (appTranslator->load(resource)) {
        QCoreApplication::installTranslator(appTranslator);
        g_appTranslator = appTranslator;
        qCInfo(logApp) << "Interface language switched to" << code << "via" << resource;
    } else {
        qCWarning(logApp) << "Translation not found:" << resource;
        delete appTranslator;
        g_currentLanguage = QLatin1String(kSourceLanguage);
        return true;
    }

    auto *qtTranslator = new QTranslator(app);
    const QString qtDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtTranslator->load(QStringLiteral("qtbase_%1").arg(code), qtDir)) {
        QCoreApplication::installTranslator(qtTranslator);
        g_qtTranslator = qtTranslator;
    } else {
        delete qtTranslator;
    }
    return true;
}

QString install(QCoreApplication *app, const QString &configured)
{
    const QString code = resolveLanguage(configured);
    qCInfo(logApp) << "UI language:" << code << "(setting:" << configured
                   << ", system locale:" << QLocale::system().name() << ")";
    apply(app, code);
    return currentLanguage();
}

} // namespace Localization
