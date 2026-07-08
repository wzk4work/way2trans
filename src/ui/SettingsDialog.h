#pragma once

#include <QDialog>

#include <functional>

class QLineEdit;
class QLabel;

// Settings dialog (DESIGN.md §7): General / Hotkeys / Engines / OCR / Platform.
//
// Every control reads its current value from Settings and writes back through
// Settings on change, so edits take effect immediately via the Settings signal
// broadcast (no restart, DESIGN.md §10). The engine tab offers a connectivity
// test button per engine; the platform tab shows the detected session and lets
// the user force a backend override for troubleshooting.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // Callback the connectivity-test button uses to run a live translation
    // through one engine. The app supplies it (it owns the engine subsystem, so
    // ui/ never links engines/). done(ok, message) reports the outcome async.
    using EngineTester = std::function<void(const QString &engineId, std::function<void(bool, QString)> done)>;

    explicit SettingsDialog(QWidget *parent = nullptr);

    // Provide the engine connectivity tester (see EngineTester). Optional; when
    // unset the test buttons report that testing is unavailable.
    void setEngineTester(EngineTester tester) { m_engineTester = std::move(tester); }

private:
    QWidget *buildGeneralTab();
    QWidget *buildHotkeysTab();
    QWidget *buildEnginesTab();
    QWidget *buildOcrTab();
    QWidget *buildPlatformTab();

    // Run a quick live translation through one engine and report success/failure
    // inline (connectivity test button, DESIGN.md §7).
    void testEngine(const QString &engineId, QLabel *resultLabel);

    EngineTester m_engineTester;
};
