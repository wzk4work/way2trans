#include "Application.h"

#include "CaptureOverlay.h"
#include "DBusService.h"
#include "HotkeyBackend.h"
#include "LangDetector.h"
#include "LibraryService.h"
#include "Logging.h"
#include "MainWindow.h"
#include "OcrService.h"
#include "PlatformFactory.h"
#include "PopupPlacement.h"
#include "PopupWindow.h"
#include "SelectionSource.h"
#include "ServiceInstances.h"
#include "ServicesPage.h"
#include "Settings.h"
#include "TranslateService.h"
#include "TrayIcon.h"
#include "engines/EngineRegistry.h"
#include "engines/TranslateEngine.h"
#include "engines/TranslateTypes.h"
#include "screenshot/ScreenshotBackend.h"

#include <QClipboard>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QEventLoop>
#include <QImage>
#include <QKeySequence>
#include <QScreen>
#include <QStringList>
#include <QTimer>

#include <cstdio>

namespace {
// Valid `trigger` sub-verbs and their D-Bus method names.
QString dbusMethodForTrigger(const QString &trigger)
{
    if (trigger == QLatin1String("selection"))
        return QStringLiteral("TriggerSelection");
    if (trigger == QLatin1String("capture"))
        return QStringLiteral("TriggerCapture");
    if (trigger == QLatin1String("capture-ocr"))
        return QStringLiteral("TriggerCaptureOcr");
    if (trigger == QLatin1String("clipboard"))
        return QStringLiteral("TriggerClipboard");
    if (trigger == QLatin1String("show-main"))
        return QStringLiteral("ShowMain");
    return {};
}
} // namespace

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    setApplicationName(QStringLiteral("way2trans"));
    setApplicationDisplayName(QStringLiteral("way2trans"));
    setApplicationVersion(QStringLiteral("0.1.0"));
    setOrganizationName(QStringLiteral("way2trans"));
    setDesktopFileName(QStringLiteral(WAY2TRANS_APP_ID));

    // The tray/window own the process lifetime; closing the main window must
    // not quit the daemon.
    setQuitOnLastWindowClosed(false);
}

Application::~Application() = default;

CliAction Application::parseCommandLine(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("way2trans - Linux word/screenshot translation tool"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption textOption(QStringList {QStringLiteral("text")},
        QStringLiteral("Translate the given text and print the result."), QStringLiteral("text"));
    parser.addOption(textOption);

    // Headless verification entry (DESIGN.md §13 M3): OCR an image file, then
    // translate the recognised text, printing both. Not part of the daemon.
    QCommandLineOption ocrTestOption(QStringList {QStringLiteral("ocr-test")},
        QStringLiteral("OCR the given image, translate the text, print the result and exit."), QStringLiteral("image"));
    parser.addOption(ocrTestOption);

    // Positional: `trigger <action>`.
    parser.addPositionalArgument(
        QStringLiteral("command"), QStringLiteral("trigger <selection|capture|capture-ocr|clipboard|show-main>"));

    auto invalid = [](const QString &reason) {
        CliAction a;
        a.kind = CliAction::Kind::Invalid;
        a.errorText = reason;
        return a;
    };

    if (!parser.parse(arguments)) {
        return invalid(parser.errorText());
    }

    // Honour the built-in --help / --version options: print and exit cleanly
    // instead of treating them as an actionable command.
    if (parser.isSet(QStringLiteral("help")))
        parser.showHelp(0); // does not return
    if (parser.isSet(QStringLiteral("version")))
        parser.showVersion(); // does not return

    if (parser.isSet(ocrTestOption)) {
        CliAction action;
        action.kind = CliAction::Kind::OcrTest;
        action.imagePath = parser.value(ocrTestOption);
        return action;
    }

    if (parser.isSet(textOption)) {
        CliAction action;
        action.kind = CliAction::Kind::TranslateText;
        action.text = parser.value(textOption);
        return action;
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        return CliAction {}; // Kind::RunDaemon by default
    }

    const QString command = positional.first();
    if (command == QLatin1String("trigger")) {
        if (positional.size() < 2) {
            return invalid(
                QStringLiteral("'trigger' requires an action: selection|capture|capture-ocr|clipboard|show-main"));
        }
        const QString action = positional.at(1);
        if (dbusMethodForTrigger(action).isEmpty()) {
            return invalid(QStringLiteral("unknown trigger action: %1").arg(action));
        }
        CliAction a;
        a.kind = CliAction::Kind::Trigger;
        a.trigger = action;
        return a;
    }

    return invalid(QStringLiteral("unknown command: %1").arg(command));
}

bool Application::registerDBusService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(w2tApp) << "Session bus not available; running without single-instance/D-Bus";
        return false;
    }

    // Publish the object first, then claim the well-known name. If the name is
    // already owned, another instance is running.
    // Parented to this; Qt owns it. We only null the pointer back out on failure.
    m_dbusAdaptor = new DBusService(this);
    if (!bus.registerObject(QString::fromLatin1(w2t::dbus::kObjectPath), this, QDBusConnection::ExportAdaptors)) {
        qCWarning(w2tApp) << "Failed to register D-Bus object at" << w2t::dbus::kObjectPath;
        delete m_dbusAdaptor;
        m_dbusAdaptor = nullptr;
        return false;
    }

    if (!bus.registerService(QString::fromLatin1(w2t::dbus::kServiceName))) {
        // Name taken -> another instance owns it.
        bus.unregisterObject(QString::fromLatin1(w2t::dbus::kObjectPath));
        delete m_dbusAdaptor;
        m_dbusAdaptor = nullptr;
        return false;
    }

    qCInfo(w2tApp) << "Registered D-Bus service" << w2t::dbus::kServiceName << "at" << w2t::dbus::kObjectPath;
    return true;
}

bool Application::tryBecomePrimary()
{
    return registerDBusService();
}

int Application::runAsPrimary(const CliAction &initialAction)
{
    Q_ASSERT(m_dbusAdaptor); // tryBecomePrimary() must have succeeded first.

    m_mainWindow = std::make_unique<MainWindow>();

    setupServices();

    // The main window's tabs consume the shared translate + library services
    // (DESIGN.md §7/§8). Injected after setupServices() built them.
    m_mainWindow->setServices(m_translateService, m_libraryService);
    connect(m_mainWindow.get(), &MainWindow::inputTranslationRequested, this, [this](const QString &text) {
        m_currentTrigger = QStringLiteral("input");
        m_popupSourceEditable = false;
        m_translateService->translate(text);
    });
    // Settings is a page inside the main window now. Inject the connectivity
    // tester (the app owns the engine subsystem, so ui/ never links engines/)
    // and apply engine config edits behind a short debounce, since the page
    // writes through to Settings on every keystroke.
    m_mainWindow->servicesPage()->setEngineTester(
        [this](const QString &engineId, std::function<void(bool, QString)> done) {
            testEngineConnectivity(engineId, std::move(done));
        });
    m_engineReloadTimer = new QTimer(this);
    m_engineReloadTimer->setSingleShot(true);
    m_engineReloadTimer->setInterval(600);
    connect(m_engineReloadTimer, &QTimer::timeout, this, [this]() {
        if (m_translateService)
            m_translateService->reloadEngines();
    });
    connect(
        m_mainWindow->servicesPage(), &ServicesPage::enginesChanged, m_engineReloadTimer, qOverload<>(&QTimer::start));

    m_tray = std::make_unique<TrayIcon>(this);
    if (m_tray->isAvailable()) {
        m_tray->show();
    } else {
        qCInfo(w2tApp) << "No system tray available; running without a tray icon";
    }

    // Perform the action this invocation asked for.
    dispatch(initialAction);

    return exec();
}

int Application::runOcrTest(const QString &imagePath)
{
    // Standalone: build just the OCR + translate services (no tray, no D-Bus)
    // and run a bounded local event loop. This is the M3 acceptance path
    // (DESIGN.md §13): prove Tesseract recognises text and it round-trips
    // through the translator, scriptably and without interactive selection.
    QImage image(imagePath);
    if (image.isNull()) {
        fprintf(stderr, "way2trans: could not load image: %s\n", qUtf8Printable(imagePath));
        return 2;
    }

    auto *ocr = new OcrService(this);
    auto *translator = new TranslateService(this);
    if (!ocr->hasBackend()) {
        fprintf(stderr, "way2trans: %s\n", qUtf8Printable(ocr->unavailableHint()));
        return 3;
    }

    QEventLoop loop;
    int exitCode = 1;

    // Safety timeout so the test never hangs CI.
    QTimer::singleShot(30000, &loop, [&loop]() { loop.quit(); });

    connect(ocr, &OcrService::failed, &loop, [&](const QString &reason) {
        fprintf(stderr, "way2trans: OCR failed: %s\n", qUtf8Printable(reason));
        loop.quit();
    });
    connect(ocr, &OcrService::recognized, this, [&](const QString &text) {
        fprintf(stdout, "OCR: %s\n", qUtf8Printable(text));
        fflush(stdout);
        // Now translate the recognised text and print the first engine result.
        connect(translator, &TranslateService::resultReady, &loop, [&](const TranslateResult &result) {
            if (result.ok()) {
                fprintf(stdout, "TR[%s]: %s\n", qUtf8Printable(result.engineId), qUtf8Printable(result.translatedText));
                fflush(stdout);
                exitCode = 0;
                loop.quit();
            } else {
                fprintf(stderr, "way2trans: translate error [%s]: %s\n", qUtf8Printable(result.engineId),
                    qUtf8Printable(result.error));
            }
        });
        translator->translate(text);
    });

    ocr->recognize(image, {QStringLiteral("en"), QStringLiteral("zh")});
    loop.exec();
    return exitCode;
}

bool Application::forwardToRunningInstance(const CliAction &action)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(w2tApp) << "Cannot forward action: session bus not available";
        return false;
    }

    QDBusInterface iface(QString::fromLatin1(w2t::dbus::kServiceName), QString::fromLatin1(w2t::dbus::kObjectPath),
        QString::fromLatin1(w2t::dbus::kInterfaceName), bus);
    if (!iface.isValid()) {
        qCWarning(w2tApp) << "Cannot forward action: running instance interface invalid:"
                          << iface.lastError().message();
        return false;
    }

    if (action.kind == CliAction::Kind::TranslateText) {
        QDBusReply<QString> reply = iface.call(QStringLiteral("Translate"), action.text);
        if (!reply.isValid()) {
            qCWarning(w2tApp) << "Translate forward failed:" << reply.error().message();
            return false;
        }
        // Print the running instance's result to stdout for scripting.
        fprintf(stdout, "%s\n", qUtf8Printable(reply.value()));
        return true;
    }

    const QString method
        = dbusMethodForTrigger(action.kind == CliAction::Kind::Trigger ? action.trigger : QStringLiteral("show-main"));
    QDBusReply<void> reply = iface.call(method);
    if (!reply.isValid()) {
        qCWarning(w2tApp) << "Forward of" << method << "failed:" << reply.error().message();
        return false;
    }
    qCInfo(w2tApp) << "Forwarded" << method << "to running instance";
    return true;
}

void Application::dispatch(const CliAction &action)
{
    switch (action.kind) {
    case CliAction::Kind::Trigger: {
        if (action.trigger == QLatin1String("selection"))
            triggerSelection();
        else if (action.trigger == QLatin1String("capture"))
            triggerCapture();
        else if (action.trigger == QLatin1String("capture-ocr"))
            triggerCaptureOcr();
        else if (action.trigger == QLatin1String("clipboard"))
            triggerClipboard();
        else if (action.trigger == QLatin1String("show-main"))
            showMainWindow();
        break;
    }
    case CliAction::Kind::TranslateText:
        fprintf(stdout, "%s\n", qUtf8Printable(translate(action.text)));
        break;
    case CliAction::Kind::OcrTest:
        // Handled before the daemon path in main(); reaching here would mean a
        // stray forward. Ignore rather than run inside the resident instance.
        qCWarning(w2tApp) << "OcrTest action reached dispatch(); ignoring";
        break;
    case CliAction::Kind::RunDaemon:
        // Bare launch: just sit resident. Show the window so first-run users
        // see something; later milestones may make this configurable.
        showMainWindow();
        break;
    case CliAction::Kind::Invalid:
        qCWarning(w2tApp) << "Ignoring invalid action:" << action.errorText;
        break;
    }
}

void Application::setupServices()
{
    PlatformFactory::logProbeSummary();

    // Must run before the popup QWindow is created so the Wayland layer-shell
    // integration is installed in time (no-op on X11).
    PlatformFactory::prepareWindowing();

    // Core translation service (owns the shared QNetworkAccessManager + engines).
    m_translateService = new TranslateService(this);

    // History + vocabulary storage (DESIGN.md §8). A failed open degrades to a
    // no-op facade so the rest of the app keeps working without persistence.
    m_libraryService = new LibraryService(this);

    // Result popup + placement strategy.
    m_popup = std::make_unique<PopupWindow>();
    m_popupPlacement = PlatformFactory::createPopupPlacement();
    connect(m_popup.get(), &PopupWindow::retryRequested, m_translateService, &TranslateService::retry);
    // Editing the recognised text in the OCR popup re-runs the translation.
    connect(m_popup.get(), &PopupWindow::sourceEdited, this, [this](const QString &newText) {
        m_currentTrigger = QStringLiteral("input"); // an edited re-translation
        m_popupSourceEditable = true; // keep the source editable across re-runs
        m_translateService->translate(newText);
    });
    // Vocabulary star: save/remove the current word (DESIGN.md §8.3).
    connect(m_popup.get(), &PopupWindow::saveRequested, this, [this](const QString &word, const QString &context) {
        if (m_libraryService)
            m_libraryService->addToVocabulary(m_lastGoodResult, word, context);
    });
    connect(m_popup.get(), &PopupWindow::unsaveRequested, this, [this](const QString &word) {
        if (!m_libraryService)
            return;
        // Mirror the lang derivation used when saving so the remove matches.
        const QString lang = m_lastGoodResult.detectedSourceLang.isEmpty() ? LangDetector::detectLang(word)
                                                                           : m_lastGoodResult.detectedSourceLang;
        m_libraryService->removeFromVocabulary(word, lang);
    });
    connect(m_translateService, &TranslateService::requestStarted, this,
        [this](const TranslateRequest &req, const QStringList &ids) {
            m_currentRequest = req;
            m_historyRecorded = false;
            m_autoCopied = false;
            m_lastGoodResult = {};
            QHash<QString, QString> names;
            for (const QString &id : ids)
                names.insert(id, m_translateService->engineDisplayName(id));
            // The source box is editable only for the screenshot/OCR flow so the
            // user can correct a misrecognition and re-translate (DESIGN.md §3.2).
            m_popup->beginRequest(req, ids, names, m_popupSourceEditable);
        });
    connect(m_translateService, &TranslateService::partialResult, this,
        [this](const QString &engineId, const QString &delta) { m_popup->applyPartial(engineId, delta); });
    connect(m_translateService, &TranslateService::resultReady, this, [this](const TranslateResult &result) {
        if (result.ok())
            qCInfo(w2tApp) << "result" << result.engineId << "->" << result.translatedText.left(60);
        else
            qCWarning(w2tApp) << "result" << result.engineId << "error:" << result.error;
        m_popup->applyResult(result);

        if (result.ok()) {
            // Auto-copy the first successful result if enabled (DESIGN.md §10).
            if (!m_autoCopied && Settings::instance().value(QStringLiteral("general/autoCopyResult"), false).toBool()) {
                m_autoCopied = true;
                QApplication::clipboard()->setText(result.translatedText);
            }
            // Record history once per request, on the first successful result
            // (DESIGN.md §8.2). Remember it as the snapshot the star will save.
            m_lastGoodResult = result;
            if (!m_historyRecorded) {
                m_historyRecorded = true;
                if (m_libraryService)
                    m_libraryService->recordHistory(m_currentTrigger, m_currentRequest, result);
            }
            // Light the star if this word is already saved (DESIGN.md §8.3).
            if (m_libraryService) {
                const QString word = (result.dict.has_value() && !result.dict->word.isEmpty())
                    ? result.dict->word
                    : m_currentRequest.text.trimmed();
                const QString lang
                    = result.detectedSourceLang.isEmpty() ? m_currentRequest.sourceLang : result.detectedSourceLang;
                m_popup->setSaved(m_libraryService->isSaved(word, lang));
            }
        }
    });

    // Platform selection source: PRIMARY selection -> translate.
    m_selectionSource = PlatformFactory::createSelectionSource(this);
    connect(m_selectionSource, &SelectionSource::selectionFetched, this,
        [this](const QString &text) { translateSelectionText(text); });
    connect(m_selectionSource, &SelectionSource::fetchFailed, this,
        [](const QString &reason) { qCWarning(w2tApp) << "selection fetch failed:" << reason; });

    // Global hotkey backend. Registration success/failure is logged; a failure
    // just means the user drives the action via `way2trans trigger selection`.
    m_hotkeyBackend = PlatformFactory::createHotkeyBackend(this);
    connect(m_hotkeyBackend, &HotkeyBackend::activated, this, [this](const QString &actionId) {
        if (actionId == QLatin1String("translate-selection"))
            triggerSelection();
        else if (actionId == QLatin1String("translate-capture"))
            triggerCapture();
        else if (actionId == QLatin1String("capture-ocr"))
            triggerCaptureOcr();
        else if (actionId == QLatin1String("translate-clipboard"))
            triggerClipboard();
        else if (actionId == QLatin1String("show-main"))
            showMainWindow();
    });

    const QString seq = Settings::instance()
                            .value(QStringLiteral("hotkeys/translate-selection"), QStringLiteral("Ctrl+Alt+D"))
                            .toString();
    const bool grabbed = m_hotkeyBackend->registerAction(
        QStringLiteral("translate-selection"), QKeySequence(seq), tr("Translate selection"));
    if (grabbed)
        qCInfo(w2tApp) << "translate-selection hotkey registered:" << seq;
    else
        qCInfo(w2tApp) << "translate-selection hotkey not grabbed (" << m_hotkeyBackend->name()
                       << "); use `way2trans trigger selection`";

    const QString captureSeq = Settings::instance()
                                   .value(QStringLiteral("hotkeys/translate-capture"), QStringLiteral("Ctrl+Alt+S"))
                                   .toString();
    const bool captureGrabbed = m_hotkeyBackend->registerAction(
        QStringLiteral("translate-capture"), QKeySequence(captureSeq), tr("Translate screenshot"));
    if (captureGrabbed)
        qCInfo(w2tApp) << "translate-capture hotkey registered:" << captureSeq;
    else
        qCInfo(w2tApp) << "translate-capture hotkey not grabbed (" << m_hotkeyBackend->name()
                       << "); use `way2trans trigger capture`";

    const QString ocrSeq = Settings::instance()
                               .value(QStringLiteral("hotkeys/capture-ocr"), QStringLiteral("Ctrl+Alt+Shift+S"))
                               .toString();
    const bool ocrGrabbed = m_hotkeyBackend->registerAction(
        QStringLiteral("capture-ocr"), QKeySequence(ocrSeq), tr("Screenshot OCR to clipboard"));
    if (ocrGrabbed)
        qCInfo(w2tApp) << "capture-ocr hotkey registered:" << ocrSeq;
    else
        qCInfo(w2tApp) << "capture-ocr hotkey not grabbed (" << m_hotkeyBackend->name()
                       << "); use `way2trans trigger capture-ocr`";

    // --- M3 screenshot translation ---------------------------------------
    m_screenshotBackend = PlatformFactory::createScreenshotBackend(this);
    connect(m_screenshotBackend, &ScreenshotBackend::captured, this, &Application::onScreensCaptured);
    connect(m_screenshotBackend, &ScreenshotBackend::captureFailed, this, [this](const QString &reason) {
        qCWarning(w2tApp) << "screenshot capture failed:" << reason;
        m_captureInProgress = false;
    });

    m_ocrService = new OcrService(this);
    connect(m_ocrService, &OcrService::recognized, this, [this](const QString &text) {
        qCInfo(w2tApp) << "OCR text (" << text.size() << "chars):" << text.left(60);
        if (text.trimmed().isEmpty()) {
            m_captureInProgress = false;
            if (m_captureMode == CaptureMode::SilentOcr && m_tray && m_tray->isAvailable())
                m_tray->notifyOcrEmpty();
            return;
        }
        // Silent capture OCR (DESIGN.md §3.4): the text goes straight to the
        // clipboard — no popup, no translation, no history row.
        if (m_captureMode == CaptureMode::SilentOcr) {
            m_captureInProgress = false;
            QApplication::clipboard()->setText(text);
            if (m_tray && m_tray->isAvailable())
                m_tray->notifyOcrCopied(text.left(80));
            return;
        }
        // Show the popup beside the selection, then translate (DESIGN.md §3.2).
        if (m_popup && m_popupPlacement) {
            m_popup->show();
            if (m_captureAnchor.isValid())
                m_popup->move(m_captureAnchor.bottomLeft());
            else
                m_popupPlacement->place(m_popup->windowHandle(), m_popup->sizeHint());
            m_popup->raise();
            m_popup->activateWindow();
        }
        m_currentTrigger = QStringLiteral("capture");
        // The capture flow is done once translation is kicked off: release the
        // re-entry guard so the hotkey can start a new capture. Editability of
        // the popup source is a separate flag — it stays on so the user can
        // correct the recognised text and re-translate.
        m_captureInProgress = false;
        m_popupSourceEditable = true;
        m_translateService->translate(text);
    });
    connect(m_ocrService, &OcrService::failed, this, [this](const QString &reason) {
        qCWarning(w2tApp) << "OCR failed:" << reason;
        m_captureInProgress = false;
        if (m_tray && m_tray->isAvailable())
            m_tray->notifyOcrUnavailable(reason);
    });
    qCInfo(w2tApp) << "OCR backends:" << m_ocrService->availableBackendIds();
    if (!m_ocrService->hasBackend())
        qCWarning(w2tApp) << "No OCR backend available:" << m_ocrService->unavailableHint();
}

void Application::translateSelectionText(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        qCInfo(w2tApp) << "empty selection; showing nothing";
        if (m_tray && m_tray->isAvailable())
            m_tray->notifyNoSelection();
        return;
    }

    // Show the popup (skeleton) at the cursor before results arrive, then kick
    // off the translation.
    if (m_popup && m_popupPlacement) {
        m_popup->show();
        m_popupPlacement->place(m_popup->windowHandle(), m_popup->sizeHint());
        m_popup->raise();
        m_popup->activateWindow();
    }
    m_currentTrigger = QStringLiteral("selection");
    m_popupSourceEditable = false;
    m_translateService->translate(text);
}

// --- Actions ---------------------------------------------------------------

void Application::triggerSelection()
{
    qCInfo(w2tApp) << "triggerSelection";
    if (!m_selectionSource) {
        qCWarning(w2tApp) << "triggerSelection before services ready";
        return;
    }
    m_selectionSource->fetchSelection();
}

void Application::triggerCapture()
{
    qCInfo(w2tApp) << "triggerCapture";
    startCapture(CaptureMode::Translate);
}

void Application::triggerCaptureOcr()
{
    qCInfo(w2tApp) << "triggerCaptureOcr";
    startCapture(CaptureMode::SilentOcr);
}

void Application::startCapture(CaptureMode mode)
{
    if (!m_screenshotBackend || !m_ocrService) {
        qCWarning(w2tApp) << "capture requested before services ready";
        return;
    }
    if (m_captureInProgress) {
        qCInfo(w2tApp) << "capture already in progress; ignoring";
        return;
    }
    if (!m_ocrService->hasBackend()) {
        // No OCR: don't leave the user staring at an overlay they can't use.
        qCWarning(w2tApp) << "capture requested but no OCR backend:" << m_ocrService->unavailableHint();
        if (m_tray && m_tray->isAvailable())
            m_tray->notifyOcrUnavailable(m_ocrService->unavailableHint());
        return;
    }
    m_captureMode = mode;
    m_captureInProgress = true;
    m_screenshotBackend->captureFullScreen();
}

void Application::onScreensCaptured(const QList<ScreenImage> &screens)
{
    closeOverlays();
    if (screens.isEmpty()) {
        qCWarning(w2tApp) << "capture returned no screens";
        m_captureInProgress = false;
        return;
    }

    // One overlay per captured screen (multi-monitor, DESIGN.md §7). If a single
    // composited frame was returned (portal/grim), slice it per screen so each
    // overlay shows the right pixels at its geometry.
    const QList<QScreen *> screenList = QGuiApplication::screens();
    const bool composited = screens.size() == 1 && screenList.size() > 1;

    auto makeOverlay = [this](const QImage &frame, const QRect &geom) {
        auto *overlay = new CaptureOverlay(frame, geom);
        connect(overlay, &CaptureOverlay::regionSelected, this, &Application::onRegionSelected);
        connect(overlay, &CaptureOverlay::cancelled, this, [this]() {
            qCInfo(w2tApp) << "capture cancelled";
            closeOverlays();
            m_captureInProgress = false;
        });
        m_overlays << overlay;
        overlay->show();
        overlay->raise();
        overlay->activateWindow();
    };

    if (composited) {
        const ScreenImage &full = screens.first();
        for (QScreen *screen : screenList) {
            const QRect geom = screen->geometry();
            // Map this screen's logical geometry into the composited frame's
            // pixel space (relative to the frame's virtual origin).
            const qreal sx = static_cast<qreal>(full.image.width()) / full.geometry.width();
            const qreal sy = static_cast<qreal>(full.image.height()) / full.geometry.height();
            const QRect src(
                QPoint(qRound((geom.x() - full.geometry.x()) * sx), qRound((geom.y() - full.geometry.y()) * sy)),
                QSize(qRound(geom.width() * sx), qRound(geom.height() * sy)));
            makeOverlay(full.image.copy(src), geom);
        }
    } else {
        for (const ScreenImage &shot : screens)
            makeOverlay(shot.image, shot.geometry);
    }
}

void Application::onRegionSelected(const QImage &cropped, const QRect &screenRect)
{
    qCInfo(w2tApp) << "region selected" << screenRect << "image" << cropped.size();
    closeOverlays();
    m_captureAnchor = screenRect;
    startOcrAndTranslate(cropped, screenRect);
}

void Application::startOcrAndTranslate(const QImage &image, const QRect &anchorRect)
{
    Q_UNUSED(anchorRect);
    if (image.isNull()) {
        qCWarning(w2tApp) << "empty crop; nothing to OCR";
        m_captureInProgress = false;
        return;
    }
    // Hint the OCR at both languages so mixed-script captures resolve; the
    // service maps hints to whatever language packs are installed.
    m_ocrService->recognize(image, {QStringLiteral("en"), QStringLiteral("zh")});
}

void Application::closeOverlays()
{
    for (CaptureOverlay *overlay : std::as_const(m_overlays)) {
        if (overlay)
            overlay->close(); // WA_DeleteOnClose frees it
    }
    m_overlays.clear();
}

void Application::triggerClipboard()
{
    qCInfo(w2tApp) << "triggerClipboard (M0 stub)";
}

void Application::showMainWindow()
{
    qCInfo(w2tApp) << "showMainWindow";
    if (!m_mainWindow) {
        // Can happen if a stray D-Bus call arrives before runAsPrimary built it.
        m_mainWindow = std::make_unique<MainWindow>();
    }
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void Application::openSettings()
{
    qCInfo(w2tApp) << "openSettings";
    // Settings lives inside the main window: bring it up on the settings page.
    showMainWindow();
    m_mainWindow->showSettings();
}

void Application::testEngineConnectivity(const QString &engineId, std::function<void(bool, QString)> done)
{
    // Connectivity test: run a one-shot "hello" through a throwaway engine
    // instance so the settings page can report reachability without touching
    // the live engine set. `engineId` is a service instance id; resolve its
    // engine type and bind the instance's config group.
    const QString group = ServiceInstances::configGroup(engineId);
    const QString type = Settings::instance().value(group + QStringLiteral("/type")).toString();
    auto *engine = EngineRegistry::create(type.isEmpty() ? engineId : type, m_translateService->networkManager(), this);
    if (!engine) {
        done(false, tr("Unknown engine"));
        return;
    }
    if (!type.isEmpty())
        engine->setInstance(engineId, QString(), group);
    // Own the engine only for the duration of this test.
    auto *guard = new QTimer(engine);
    guard->setSingleShot(true);
    connect(engine, &TranslateEngine::resultReady, this, [engine, done](const TranslateResult &r) {
        done(r.ok(), r.ok() ? r.translatedText.left(40) : r.error);
        engine->deleteLater();
    });
    connect(guard, &QTimer::timeout, this, [engine, done]() {
        done(false, tr("Timed out"));
        engine->deleteLater();
    });
    guard->start(10000);
    TranslateRequest req;
    req.text = QStringLiteral("hello");
    req.sourceLang = QStringLiteral("auto");
    req.targetLang = QStringLiteral("zh-CN");
    engine->translate(req);
}

QString Application::translate(const QString &text)
{
    // M0: no translation engines yet (M1). Return a placeholder.
    qCInfo(w2tApp) << "translate (M0 stub) for text of length" << text.size();
    return QStringLiteral("[way2trans placeholder] %1").arg(text);
}
