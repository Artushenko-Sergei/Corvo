// Localization.h - UI language selection and translator installation.
#ifndef CORVO_LOCALIZATION_H
#define CORVO_LOCALIZATION_H

#include <QString>

class QCoreApplication;

/**
 * Interface language handling.
 *
 * The source strings in this project are Russian, so "ru" needs no translator;
 * English, German and Ukrainian are shipped as .qm files compiled from
 * translations/Corvo_<code>.ts and embedded into the binary under
 * ":/i18n/". Qt's own strings (standard dialog buttons, spell-check menus) are
 * loaded from the Qt installation when available.
 */
namespace Localization {

/// Turns a stored setting ("system", "ru", "en", "de", "uk") into a concrete
/// language code, resolving "system" against the current locale and falling back
/// to English for locales this application has no translation for.
QString resolveLanguage(const QString &configured);

/**
 * Installs the translators for the configured language, and makes it the
 * default QLocale so dates and numbers match the interface.
 * Returns the language code actually applied.
 */
QString install(QCoreApplication *app, const QString &configured);

/**
 * Switches the interface language while the application is running: the previous
 * translators are removed and the new ones installed, which makes Qt deliver
 * QEvent::LanguageChange to every widget. Returns true when the language really
 * changed.
 */
bool apply(QCoreApplication *app, const QString &code);

/// Language currently in effect.
QString currentLanguage();

/// Maps a page/browser language tag ("de", "ru-RU", "pt-BR") onto a language this
/// build has an interface for; empty when there is no match.
QString matchSupported(const QString &tag);

/// Full locale name for a language code, e.g. "de" -> "de-DE".
QString localeName(const QString &code);

/**
 * Value for the Accept-Language header and for Chromium's --lang switch.
 *
 * This is what makes WhatsApp Web itself appear in the chosen language: the page
 * looks at navigator.language (fed by --lang) and at the Accept-Language header,
 * neither of which has anything to do with the Qt translations.
 */
QString acceptLanguage(const QString &code);

} // namespace Localization

#endif // CORVO_LOCALIZATION_H
