#include "DeviceCheckDialog.h"

#include "AppInfo.h"
#include "Logger.h"
#include "WebPage.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineView>

namespace {
const char *kResultPrefix = "CORVO-RESULT:";
const char *kPageUrl = "qrc:/html/devicecheck.html";
} // namespace

DeviceCheckDialog::DeviceCheckDialog(QWebEngineProfile *profile, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Проверка камеры и микрофона"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral(CORVO_APP_ID),
                                   QIcon(QStringLiteral(":/icons/Corvo.png"))));
    resize(940, 720);

    m_view = new QWebEngineView(this);
    m_page = new WebPage(profile, WebPage::UrlPolicy::LocalDiagnostics, m_view);
    m_view->setPage(m_page);
    m_view->setMinimumHeight(380);

    m_report = new QPlainTextEdit(this);
    m_report->setReadOnly(true);
    m_report->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_report->setMinimumHeight(160);
    m_report->setPlaceholderText(tr("Здесь появится отчёт об устройствах."));

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_view);
    splitter->addWidget(m_report);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    m_enumerateButton = new QPushButton(tr("Проверить устройства"), this);
    auto *copyButton = new QPushButton(tr("Копировать отчёт"), this);
    auto *closeButton = new QPushButton(tr("Закрыть"), this);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_enumerateButton);
    buttons->addWidget(copyButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Страница использует тот же профиль WebEngine, что и WhatsApp, "
           "поэтому результат совпадает с тем, что видит WhatsApp Web."), this));
    layout->addWidget(splitter, 1);
    layout->addLayout(buttons);

    connect(m_enumerateButton, &QPushButton::clicked, this, &DeviceCheckDialog::runEnumeration);
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        m_report->selectAll();
        m_report->copy();
        m_report->moveCursor(QTextCursor::End);
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    connect(m_page, &QWebEnginePage::titleChanged, this, &DeviceCheckDialog::handleTitleChanged);
    connect(m_page, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        m_pageReady = ok;
        if (!ok) {
            appendReport(tr("Не удалось загрузить диагностическую страницу."));
            qCWarning(logMedia) << "Device check page failed to load";
            return;
        }
        qCInfo(logMedia) << "Device check page loaded";
        sendPageTranslations();
        if (m_enumerationPending) {
            m_enumerationPending = false;
            runEnumeration();
        }
    });

    reportSystemDevices();
    m_page->load(QUrl(QLatin1String(kPageUrl)));
}

DeviceCheckDialog::~DeviceCheckDialog()
{
    // Make sure any live camera/microphone capture is released with the dialog.
    if (m_page)
        m_page->runJavaScript(QStringLiteral("window.corvoStopAll && window.corvoStopAll();"));
}

void DeviceCheckDialog::closeEvent(QCloseEvent *event)
{
    if (m_page)
        m_page->runJavaScript(QStringLiteral("window.corvoStopAll && window.corvoStopAll();"));
    QDialog::closeEvent(event);
}

/// Hands the page its user-visible strings so it follows the application
/// language instead of shipping its own translations.
void DeviceCheckDialog::sendPageTranslations()
{
    QJsonObject strings;
    strings.insert(QStringLiteral("lang"), QLocale().name().left(2));
    strings.insert(QStringLiteral("title"), tr("Проверка камеры и микрофона"));
    strings.insert(QStringLiteral("enumerate"), tr("Проверить устройства"));
    strings.insert(QStringLiteral("camera"), tr("Включить камеру"));
    strings.insert(QStringLiteral("mic"), tr("Проверить микрофон"));
    strings.insert(QStringLiteral("stop"), tr("Остановить"));
    strings.insert(QStringLiteral("notRequested"), tr("Список устройств пока не запрашивался."));
    strings.insert(QStringLiteral("ready"), tr("Готово к проверке."));
    strings.insert(QStringLiteral("cameras"), tr("(камеры)"));
    strings.insert(QStringLiteral("microphones"), tr("(микрофоны)"));
    strings.insert(QStringLiteral("outputs"), tr("(вывод звука)"));
    strings.insert(QStringLiteral("noDevices"), tr("устройств не найдено"));
    strings.insert(QStringLiteral("unnamed"), tr("(без названия - нет разрешения)"));
    strings.insert(QStringLiteral("asking"), tr("Запрашиваем доступ к камере и микрофону..."));
    strings.insert(QStringLiteral("none"), tr("Устройства не обнаружены. "));
    strings.insert(QStringLiteral("found"), tr("Найдено камер: "));
    strings.insert(QStringLiteral("andMics"), tr(", микрофонов: "));
    strings.insert(QStringLiteral("warnings"), tr("Предупреждения: "));
    strings.insert(QStringLiteral("stopped"), tr("Захват остановлен."));
    strings.insert(QStringLiteral("openingCamera"), tr("Открываем камеру..."));
    strings.insert(QStringLiteral("openingMic"), tr("Открываем микрофон..."));
    strings.insert(QStringLiteral("cameraWorks"), tr("Камера работает: "));
    strings.insert(QStringLiteral("cameraFailed"), tr("Камера недоступна: "));
    strings.insert(QStringLiteral("micWorks"), tr("Микрофон работает: "));
    strings.insert(QStringLiteral("saySomething"), tr("скажите что-нибудь."));
    strings.insert(QStringLiteral("micFailed"), tr("Микрофон недоступен: "));
    strings.insert(QStringLiteral("defaultDevice"), tr("Устройство по умолчанию (default)"));
    strings.insert(QStringLiteral("commsDevice"), tr("Устройство для связи (communications)"));

    const QString json = QString::fromUtf8(QJsonDocument(strings).toJson(QJsonDocument::Compact));
    m_page->runJavaScript(QStringLiteral("window.corvoSetStrings && window.corvoSetStrings(%1);")
                              .arg(json));
}

void DeviceCheckDialog::appendReport(const QString &text)
{
    m_report->appendPlainText(text);
}

/// Lists /dev/video* so a missing kernel device is distinguishable from a
/// permission problem inside Chromium (section 14: "отсутствие камеры").
void DeviceCheckDialog::reportSystemDevices()
{
    appendReport(tr("=== Системные устройства ==="));

    const QStringList nodes = QDir(QStringLiteral("/dev"))
                                  .entryList({QStringLiteral("video*")}, QDir::System, QDir::Name);
    if (nodes.isEmpty()) {
        appendReport(tr("/dev/video*: не найдено ни одного устройства."));
        appendReport(tr("Проверьте модуль uvcvideo (lsmod | grep uvcvideo) и "
                        "членство пользователя в группе video."));
        qCWarning(logMedia) << "No /dev/video* nodes present";
    } else {
        for (const QString &node : nodes) {
            const QString path = QStringLiteral("/dev/") + node;
            const QFileInfo info(path);
            appendReport(QStringLiteral("%1  %2")
                             .arg(path, info.isReadable()
                                            ? tr("доступно для чтения")
                                            : tr("НЕТ ДОСТУПА (нужна группа video)")));
        }
        qCInfo(logMedia) << "Kernel video nodes:" << nodes;
    }
    appendReport(QString());
}

void DeviceCheckDialog::runEnumeration()
{
    if (!m_pageReady) {
        m_enumerationPending = true;
        return;
    }
    appendReport(tr("=== Запрос устройств через navigator.mediaDevices ==="));
    m_page->runJavaScript(QStringLiteral("window.corvoEnumerate && window.corvoEnumerate();"));
    qCInfo(logMedia) << "enumerateDevices() requested";
}

void DeviceCheckDialog::handleTitleChanged(const QString &title)
{
    const QString prefix = QLatin1String(kResultPrefix);
    if (!title.startsWith(prefix))
        return;

    const QByteArray json = title.mid(prefix.size()).toUtf8();
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(logMedia) << "Cannot parse device report:" << error.errorString();
        return;
    }

    const QJsonObject root = doc.object();

    if (root.contains(QStringLiteral("devices"))) {
        const QJsonArray devices = root.value(QStringLiteral("devices")).toArray();
        const auto printKind = [&](const char *kind) {
            appendReport(QString::fromLatin1(kind) + QLatin1Char(':'));
            int found = 0;
            for (const QJsonValue &value : devices) {
                const QJsonObject device = value.toObject();
                if (device.value(QStringLiteral("kind")).toString() != QLatin1String(kind))
                    continue;
                QString label = device.value(QStringLiteral("label")).toString();
                if (label.isEmpty())
                    label = tr("(без названия)");
                appendReport(QStringLiteral("- ") + label);
                ++found;
            }
            if (found == 0)
                appendReport(tr("- не найдено"));
            qCInfo(logMedia).noquote() << kind << ":" << found << "device(s)";
        };

        printKind("videoinput");
        printKind("audioinput");
        printKind("audiooutput");
    }

    if (root.contains(QStringLiteral("camera"))) {
        const QJsonValue camera = root.value(QStringLiteral("camera"));
        if (camera.isNull()) {
            appendReport(tr("Камера: не удалось открыть."));
        } else {
            const QJsonObject settings = camera.toObject().value(QStringLiteral("settings")).toObject();
            appendReport(tr("Камера открыта: %1 (%2x%3)")
                             .arg(camera.toObject().value(QStringLiteral("label")).toString())
                             .arg(settings.value(QStringLiteral("width")).toInt())
                             .arg(settings.value(QStringLiteral("height")).toInt()));
        }
    }

    if (root.contains(QStringLiteral("microphone"))) {
        const QJsonValue mic = root.value(QStringLiteral("microphone"));
        appendReport(mic.isNull() ? tr("Микрофон: не удалось открыть.")
                                  : tr("Микрофон открыт: %1")
                                        .arg(mic.toObject().value(QStringLiteral("label")).toString()));
    }

    const QJsonArray errors = root.value(QStringLiteral("errors")).toArray();
    for (const QJsonValue &value : errors) {
        appendReport(tr("Ошибка: ") + value.toString());
        qCWarning(logMedia) << "Media error:" << value.toString();
    }

    appendReport(QString());
}
