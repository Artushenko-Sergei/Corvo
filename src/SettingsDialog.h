// SettingsDialog.h - UI for the values stored by Settings.
#ifndef CORVO_SETTINGSDIALOG_H
#define CORVO_SETTINGSDIALOG_H

#include <QDialog>

class Settings;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

/// Editor for the persisted application settings.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(Settings *settings, QWidget *parent = nullptr);

signals:
    /// Emitted after the values have been written back to Settings.
    void settingsApplied();
    /// The user asked for the camera/microphone check (lives here since 1.2.0).
    void deviceCheckRequested();

private:
    void load();
    void apply();
    void updateDesktopEntryState();

    Settings *m_settings = nullptr;

    QComboBox *m_language = nullptr;
    QLabel *m_languageHint = nullptr;
    QLabel *m_languageScope = nullptr;
    QString m_languageOnEntry;

    QCheckBox *m_trayEnabled = nullptr;
    QCheckBox *m_closeToTray = nullptr;
    QCheckBox *m_minimizeToTray = nullptr;
    QCheckBox *m_startHidden = nullptr;
    QCheckBox *m_notifications = nullptr;
    QCheckBox *m_autostart = nullptr;
    QCheckBox *m_spellCheck = nullptr;
    QLineEdit *m_spellLanguages = nullptr;
    QSpinBox *m_zoom = nullptr;
    QLineEdit *m_downloadPath = nullptr;
    QPushButton *m_desktopEntryButton = nullptr;
    QLabel *m_desktopEntryState = nullptr;
};

#endif // CORVO_SETTINGSDIALOG_H
