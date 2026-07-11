#pragma once

#include "engines/TranslateTypes.h"

#include <QApplication>
#include <QHash>
#include <QList>
#include <QRect>
#include <QString>
#include <functional>
#include <memory>

class DBusService;
class TrayIcon;
class MainWindow;
class PopupWindow;
class PopupPlacement;
class SelectionSource;
class HotkeyBackend;
class ScreenshotBackend;
class TranslateService;
class OcrService;
class LibraryService;
class CaptureOverlay;
class QTimer;
struct ScreenImage;

// Result of parsing the command line into a single deferred action.
struct CliAction {
    enum class Kind {
        RunDaemon, // no action verb: start (or reuse) the resident instance
        Trigger, // `trigger <selection|capture|clipboard|show-main>`
        TranslateText, // `--text "..."`
        OcrTest, // `--ocr-test <image>`: OCR+translate a file, print, exit
        Invalid, // parse error / unknown verb
    };

    Kind kind = Kind::RunDaemon;
    QString trigger; // for Kind::Trigger
    QString text; // for Kind::TranslateText
    QString imagePath; // for Kind::OcrTest
    QString errorText; // for Kind::Invalid
};

// Central lifecycle orchestrator (DESIGN.md §2 app layer).
//
// Owns the single-instance handshake, the D-Bus service, the tray icon and the
// main window. On startup it tries to own the bus name io.github.way2trans; if
// that fails, another instance is already running, so it forwards the requested
// CLI action over D-Bus and exits (DESIGN.md §9).
class Application : public QApplication {
    Q_OBJECT

public:
    Application(int &argc, char **argv);
    ~Application() override;

    // Parse argv into a single deferred action. Static so callers can inspect
    // the result before deciding how to run.
    static CliAction parseCommandLine(const QStringList &arguments);

    // Try to own the bus name io.github.way2trans and publish the D-Bus
    // service. Returns true if this process is now the primary instance.
    // Returns false if the name is already owned (an instance is running) or
    // the session bus is unavailable.
    bool tryBecomePrimary();

    // Bring up the tray + window, perform the initial action, and run the Qt
    // event loop. Must only be called after tryBecomePrimary() returned true.
    // Returns the event loop exit code.
    int runAsPrimary(const CliAction &initialAction);

    // Headless verification entry (DESIGN.md §13 M3 acceptance): OCR the given
    // image via OcrService, translate the recognised text via TranslateService,
    // print "OCR:"/"TR:" lines to stdout and return an exit code. Runs its own
    // short event loop; does not need the daemon/tray. Returns 0 on success.
    int runOcrTest(const QString &imagePath);

    // Forward an action to an already-running instance over D-Bus.
    // Returns true on success. Used when the bus name is already taken.
    static bool forwardToRunningInstance(const CliAction &action);

    // --- Actions (invoked by D-Bus, tray, CLI) ---------------------------
    void triggerSelection();
    void triggerCapture();
    void triggerCaptureOcr(); // silent capture OCR: recognise to clipboard (§3.4)
    void triggerClipboard();
    void showMainWindow();
    void openSettings();
    QString translate(const QString &text);

    // History/vocabulary facade, for the tray + main window (M4). Non-null once
    // setupServices() has run; may report not-ready if the DB failed to open.
    LibraryService *libraryService() const { return m_libraryService; }

private:
    // What a capture does with the recognised text: translate in the popup
    // (§3.2) or copy it straight to the clipboard (§3.4).
    enum class CaptureMode { Translate, SilentOcr };

    // Perform a parsed action against this (primary) instance.
    void dispatch(const CliAction &action);

    // Attempt to register the service name + object on the session bus.
    // Returns false if the name is already owned by another instance.
    bool registerDBusService();

    // Build the platform backends, translate service and popup, and register the
    // configured global hotkeys. Called once when becoming the primary instance.
    void setupServices();

    // Show the result popup near the cursor and kick off a translation.
    // `trigger` is the history trigger tag: selection | clipboard.
    void startPopupTranslation(const QString &text, const QString &trigger);
    // The selection flow on top of it: empty selections get a tray hint.
    void translateSelectionText(const QString &text);

    // Run a one-shot "hello" through a throwaway engine instance and report
    // reachability (settings page connectivity test, DESIGN.md §7).
    void testEngineConnectivity(const QString &engineId, std::function<void(bool, QString)> done);

    // Screenshot-translation pipeline (DESIGN.md §3.2): capture -> overlay ->
    // OCR -> translate -> popup beside the selection. The silent variant
    // (§3.4) shares it and diverts the recognised text to the clipboard.
    void startCapture(CaptureMode mode);
    void onScreensCaptured(const QList<ScreenImage> &screens);
    void onRegionSelected(const QImage &cropped, const QRect &screenRect);
    void startOcrAndTranslate(const QImage &image, const QRect &anchorRect);
    void closeOverlays();

    // Parented to this Application (Qt manages its lifetime); non-null once
    // this process is the primary instance.
    DBusService *m_dbusAdaptor = nullptr;
    std::unique_ptr<TrayIcon> m_tray;
    std::unique_ptr<MainWindow> m_mainWindow;

    // M1 selection-translation pipeline (all owned via QObject parent = this,
    // except the placement strategy which is a plain object).
    TranslateService *m_translateService = nullptr;
    LibraryService *m_libraryService = nullptr; // history + vocabulary (M4)
    SelectionSource *m_selectionSource = nullptr;
    HotkeyBackend *m_hotkeyBackend = nullptr;
    std::unique_ptr<PopupPlacement> m_popupPlacement;
    std::unique_ptr<PopupWindow> m_popup;

    // M3 screenshot-translation pipeline.
    ScreenshotBackend *m_screenshotBackend = nullptr; // owned via parent = this
    OcrService *m_ocrService = nullptr; // owned via parent = this
    QList<CaptureOverlay *> m_overlays; // live overlays (WA_DeleteOnClose)
    QRect m_captureAnchor; // selection rect for popup placement
    bool m_captureInProgress = false; // re-entry guard: overlay/OCR still running
    // Mode of the capture in flight. One meaning only — the re-entry guard
    // above stays a separate field (AGENTS.md).
    CaptureMode m_captureMode = CaptureMode::Translate;
    bool m_popupSourceEditable = false; // popup shows an editable source (OCR flow)

    // Debounces SettingsPage::enginesChanged into reloadEngines(), so typing an
    // API key doesn't rebuild the engine set on every keystroke.
    QTimer *m_engineReloadTimer = nullptr;

    // M4 history/vocabulary bookkeeping for the in-flight request.
    QString m_currentTrigger; // selection|capture|input|clipboard
    TranslateRequest m_currentRequest; // request being translated right now
    TranslateResult m_lastGoodResult; // latest ok result (snapshot for the star)
    // History records the first *in service order* engine that succeeds — not
    // the fastest responder — so what gets stored is deterministic (§8.2). The
    // order comes from requestStarted; results collect here until the highest-
    // priority pending engine resolves.
    QStringList m_requestEngineOrder;
    QHash<QString, TranslateResult> m_requestResults;
    bool m_historyRecorded = false; // the ordered winner has been written
    bool m_autoCopied = false; // auto-copy fired for this request (§10 autoCopyResult)
};
