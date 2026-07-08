#pragma once

#include <QApplication>
#include <QString>
#include <memory>

class DBusService;
class TrayIcon;
class MainWindow;
class PopupWindow;
class PopupPlacement;
class SelectionSource;
class HotkeyBackend;
class TranslateService;
struct TranslateRequest;
struct TranslateResult;

// Result of parsing the command line into a single deferred action.
struct CliAction {
    enum class Kind {
        RunDaemon, // no action verb: start (or reuse) the resident instance
        Trigger, // `trigger <selection|capture|clipboard|show-main>`
        TranslateText, // `--text "..."`
        Invalid, // parse error / unknown verb
    };

    Kind kind = Kind::RunDaemon;
    QString trigger; // for Kind::Trigger
    QString text; // for Kind::TranslateText
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

    // Forward an action to an already-running instance over D-Bus.
    // Returns true on success. Used when the bus name is already taken.
    static bool forwardToRunningInstance(const CliAction &action);

    // --- Actions (invoked by D-Bus, tray, CLI) ---------------------------
    void triggerSelection();
    void triggerCapture();
    void triggerClipboard();
    void showMainWindow();
    void openSettings();
    QString translate(const QString &text);

private:
    // Perform a parsed action against this (primary) instance.
    void dispatch(const CliAction &action);

    // Attempt to register the service name + object on the session bus.
    // Returns false if the name is already owned by another instance.
    bool registerDBusService();

    // Build the platform backends, translate service and popup, and register the
    // configured global hotkeys. Called once when becoming the primary instance.
    void setupServices();

    // Show the result popup near the cursor and kick off a translation.
    void translateSelectionText(const QString &text);

    // Parented to this Application (Qt manages its lifetime); non-null once
    // this process is the primary instance.
    DBusService *m_dbusAdaptor = nullptr;
    std::unique_ptr<TrayIcon> m_tray;
    std::unique_ptr<MainWindow> m_mainWindow;

    // M1 selection-translation pipeline (all owned via QObject parent = this,
    // except the placement strategy which is a plain object).
    TranslateService *m_translateService = nullptr;
    SelectionSource *m_selectionSource = nullptr;
    HotkeyBackend *m_hotkeyBackend = nullptr;
    std::unique_ptr<PopupPlacement> m_popupPlacement;
    std::unique_ptr<PopupWindow> m_popup;
};
