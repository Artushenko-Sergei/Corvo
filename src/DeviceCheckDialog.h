// DeviceCheckDialog.h - "Проверка камеры" diagnostics window (section 5 and 15).
#ifndef CORVO_DEVICECHECKDIALOG_H
#define CORVO_DEVICECHECKDIALOG_H

#include <QDialog>
#include <QString>

class WebPage;
class QPlainTextEdit;
class QPushButton;
class QWebEngineProfile;
class QWebEngineView;

/**
 * Runs navigator.mediaDevices.enumerateDevices() / getUserMedia() inside a
 * bundled qrc: page that shares the application's web profile, so what it
 * reports is exactly what WhatsApp Web will see.
 *
 * The JavaScript side hands results back through document.title
 * ("QTWA-RESULT:<json>"), which this dialog formats into the report pane and
 * into the application log.
 */
class DeviceCheckDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceCheckDialog(QWebEngineProfile *profile, QWidget *parent = nullptr);
    ~DeviceCheckDialog() override;

    /// Starts an enumeration run as soon as the page is ready.
    void runEnumeration();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void appendReport(const QString &text);
    void sendPageTranslations();
    void handleTitleChanged(const QString &title);
    void reportSystemDevices();

    QWebEngineView *m_view = nullptr;
    WebPage *m_page = nullptr;
    QPlainTextEdit *m_report = nullptr;
    QPushButton *m_enumerateButton = nullptr;
    bool m_pageReady = false;
    bool m_enumerationPending = false;
};

#endif // CORVO_DEVICECHECKDIALOG_H
