#include "SettingsDialog.h"

#include "AppInfo.h"
#include "Logger.h"
#include "Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(Settings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("Настройки"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral(CORVO_APP_ID),
                                   QIcon(QStringLiteral(":/icons/Corvo.png"))));
    setMinimumWidth(560);

    // --- Интерфейс ---------------------------------------------------------
    auto *uiBox = new QGroupBox(tr("Интерфейс"), this);
    m_language = new QComboBox(uiBox);
    const QStringList codes = Settings::availableLanguages();
    for (const QString &code : codes)
        m_language->addItem(Settings::languageDisplayName(code), code);

    m_languageHint = new QLabel(uiBox);
    m_languageHint->setWordWrap(true);
    m_languageHint->hide();

    // WhatsApp Web has no language setting of its own: a linked client takes its
    // language from the phone, and only the login screen follows the browser.
    // Saying so here prevents the obvious misunderstanding.
    m_languageScope = new QLabel(
        tr("«Как в WhatsApp» — интерфейс программы подхватывает язык, на котором "
           "WhatsApp показывает чаты. Сам язык WhatsApp задаётся в приложении на "
           "телефоне."), uiBox);
    m_languageScope->setWordWrap(true);
    QFont hintFont = m_languageScope->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.92);
    m_languageScope->setFont(hintFont);
    m_languageScope->setEnabled(false);

    m_zoom = new QSpinBox(uiBox);
    m_zoom->setRange(50, 250);
    m_zoom->setSingleStep(5);
    m_zoom->setSuffix(QStringLiteral(" %"));

    // Word-wrapped labels get clipped inside a QFormLayout row (it sizes them for
    // a single line), so the two hints live in the surrounding vertical layout.
    auto *languageRow = new QFormLayout;
    languageRow->addRow(tr("Язык:"), m_language);
    auto *zoomRow = new QFormLayout;
    zoomRow->addRow(tr("Масштаб страницы:"), m_zoom);

    auto *uiLayout = new QVBoxLayout(uiBox);
    uiLayout->addLayout(languageRow);
    uiLayout->addWidget(m_languageScope);   // stays attached to the language field
    uiLayout->addWidget(m_languageHint);
    uiLayout->addLayout(zoomRow);

    // --- Окно и трей -------------------------------------------------------
    auto *trayBox = new QGroupBox(tr("Окно и трей"), this);
    m_trayEnabled = new QCheckBox(tr("Показывать значок в системном трее"), trayBox);
    m_closeToTray = new QCheckBox(tr("При закрытии окна скрывать в трей"), trayBox);
    m_minimizeToTray = new QCheckBox(tr("При минимизации скрывать в трей"), trayBox);
    m_startHidden = new QCheckBox(tr("Запускать свёрнутым в трей"), trayBox);
    m_autostart = new QCheckBox(tr("Запускать при входе в систему"), trayBox);
    auto *trayLayout = new QVBoxLayout(trayBox);
    trayLayout->addWidget(m_trayEnabled);
    trayLayout->addWidget(m_closeToTray);
    trayLayout->addWidget(m_minimizeToTray);
    trayLayout->addWidget(m_startHidden);
    trayLayout->addWidget(m_autostart);

    // --- Уведомления и устройства ------------------------------------------
    auto *mediaBox = new QGroupBox(tr("Уведомления и устройства"), this);
    m_notifications = new QCheckBox(tr("Уведомления о новых сообщениях"), mediaBox);

    auto *deviceCheckButton = new QPushButton(tr("Проверить камеру и микрофон..."), mediaBox);
    deviceCheckButton->setToolTip(
        tr("Показывает камеры и микрофоны, которые видит WhatsApp, "
           "и позволяет проверить их работу."));

    auto *mediaLayout = new QVBoxLayout(mediaBox);
    mediaLayout->addWidget(m_notifications);
    auto *deviceRow = new QHBoxLayout;
    deviceRow->addWidget(deviceCheckButton);
    deviceRow->addStretch(1);
    mediaLayout->addLayout(deviceRow);

    // --- Приложение в системе ---------------------------------------------
    auto *desktopBox = new QGroupBox(tr("Приложение в системе"), this);
    m_desktopEntryState = new QLabel(desktopBox);
    m_desktopEntryState->setWordWrap(true);
    m_desktopEntryButton = new QPushButton(tr("Добавить в меню приложений"), desktopBox);
    m_desktopEntryButton->setToolTip(
        tr("Создаёт ярлык и значки в домашнем каталоге, чтобы у программы "
           "появились название и иконка в меню, панели задач и уведомлениях."));
    auto *desktopLayout = new QVBoxLayout(desktopBox);
    desktopLayout->addWidget(m_desktopEntryState);
    auto *desktopRow = new QHBoxLayout;
    desktopRow->addWidget(m_desktopEntryButton);
    desktopRow->addStretch(1);
    desktopLayout->addLayout(desktopRow);

    // --- Прочее ------------------------------------------------------------
    auto *miscBox = new QGroupBox(tr("Дополнительно"), this);
    m_spellCheck = new QCheckBox(tr("Проверка орфографии"), miscBox);
    m_spellLanguages = new QLineEdit(miscBox);
    m_spellLanguages->setPlaceholderText(QStringLiteral("ru_RU, en_US"));
    m_downloadPath = new QLineEdit(miscBox);
    auto *chooseDownloads = new QPushButton(tr("Выбрать..."), miscBox);
    auto *downloadRow = new QHBoxLayout;
    downloadRow->addWidget(m_downloadPath, 1);
    downloadRow->addWidget(chooseDownloads);

    auto *miscLayout = new QFormLayout(miscBox);
    miscLayout->addRow(m_spellCheck);
    miscLayout->addRow(tr("Языки орфографии:"), m_spellLanguages);
    miscLayout->addRow(tr("Каталог загрузок:"), downloadRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply
                                             | QDialogButtonBox::Cancel,
                                         this);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(uiBox);
    layout->addWidget(trayBox);
    layout->addWidget(mediaBox);
    layout->addWidget(desktopBox);
    layout->addWidget(miscBox);
    layout->addWidget(buttons);

    connect(deviceCheckButton, &QPushButton::clicked,
            this, &SettingsDialog::deviceCheckRequested);
    connect(m_desktopEntryButton, &QPushButton::clicked, this, [this]() {
        if (m_settings->installDesktopEntry()) {
            QMessageBox::information(
                this, tr("Меню приложений"),
                tr("Ярлык создан. Значок и название появятся в меню и на панели "
                   "задач; в некоторых окружениях для этого нужно перезайти в сеанс."));
        } else {
            QMessageBox::warning(this, tr("Меню приложений"),
                                 tr("Не удалось создать ярлык:\n%1")
                                     .arg(Settings::userDesktopEntryPath()));
        }
        updateDesktopEntryState();
    });
    connect(chooseDownloads, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Каталог загрузок"),
                                                              m_downloadPath->text());
        if (!dir.isEmpty())
            m_downloadPath->setText(dir);
    });
    connect(m_spellCheck, &QCheckBox::toggled, m_spellLanguages, &QWidget::setEnabled);
    connect(m_language, &QComboBox::currentIndexChanged, this, [this]() {
        const bool changed = m_language->currentData().toString() != m_languageOnEntry;
        m_languageHint->setText(tr("Язык применится сразу после «Применить»."));
        m_languageHint->setVisible(changed);
    });

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SettingsDialog::apply);

    load();
}

void SettingsDialog::load()
{
    m_languageOnEntry = m_settings->language();
    const int index = m_language->findData(m_languageOnEntry);
    m_language->setCurrentIndex(index >= 0 ? index : 0);

    m_trayEnabled->setChecked(m_settings->trayEnabled());
    m_closeToTray->setChecked(m_settings->closeToTray());
    m_minimizeToTray->setChecked(m_settings->minimizeToTray());
    m_startHidden->setChecked(m_settings->startHidden());
    m_notifications->setChecked(m_settings->notificationsEnabled());
    m_autostart->setChecked(m_settings->autostartEnabled());
    m_spellCheck->setChecked(m_settings->spellCheckEnabled());
    m_spellLanguages->setText(m_settings->spellCheckLanguages().join(QStringLiteral(", ")));
    m_spellLanguages->setEnabled(m_settings->spellCheckEnabled());
    m_zoom->setValue(m_settings->zoomPercent());
    m_downloadPath->setText(m_settings->downloadPath());
    m_languageHint->hide();
    updateDesktopEntryState();
}

void SettingsDialog::updateDesktopEntryState()
{
    const bool installed = Settings::desktopEntryInstalled();
    m_desktopEntryState->setText(
        installed ? tr("Программа зарегистрирована в системе: значок и название доступны.")
                  : tr("Программа не зарегистрирована в системе, поэтому в меню и на "
                       "панели задач может не быть значка."));
    m_desktopEntryButton->setText(installed ? tr("Обновить ярлык")
                                            : tr("Добавить в меню приложений"));
}

void SettingsDialog::apply()
{
    m_settings->setLanguage(m_language->currentData().toString());
    m_settings->setTrayEnabled(m_trayEnabled->isChecked());
    m_settings->setCloseToTray(m_closeToTray->isChecked());
    m_settings->setMinimizeToTray(m_minimizeToTray->isChecked());
    m_settings->setStartHidden(m_startHidden->isChecked());
    m_settings->setNotificationsEnabled(m_notifications->isChecked());
    m_settings->setSpellCheckEnabled(m_spellCheck->isChecked());

    QStringList languages;
    const QStringList raw = m_spellLanguages->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &language : raw) {
        const QString trimmed = language.trimmed();
        if (!trimmed.isEmpty())
            languages << trimmed;
    }
    if (!languages.isEmpty())
        m_settings->setSpellCheckLanguages(languages);

    m_settings->setZoomPercent(m_zoom->value());
    m_settings->setDownloadPath(m_downloadPath->text());

    if (m_autostart->isChecked() != m_settings->autostartEnabled()) {
        if (!m_settings->setAutostartEnabled(m_autostart->isChecked())) {
            QMessageBox::warning(this, tr("Автозапуск"),
                                 tr("Не удалось изменить файл автозапуска:\n%1")
                                     .arg(Settings::autostartFilePath()));
            m_autostart->setChecked(m_settings->autostartEnabled());
        }
    }

    m_settings->sync();
    qCInfo(logSettings) << "Settings applied from dialog";
    emit settingsApplied();
}
