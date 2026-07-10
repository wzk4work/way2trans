#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;

// Settings page embedded in the main window (DESIGN.md §7): a narrow section
// list on the left (General / Hotkeys / Services / OCR / Platform) and a
// stacked pane on the right, following the layout of mainstream translators
// (Pot, STranslate).
//
// Every control reads its current value from Settings and writes back through
// Settings on change, so edits take effect immediately (no restart,
// DESIGN.md §10). The services section renders one expandable card per engine
// with an enable toggle, move up/down reordering and a connectivity test.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    // Callback the connectivity-test button uses to run a live translation
    // through one engine. The app supplies it (it owns the engine subsystem, so
    // ui/ never links engines/). done(ok, message) reports the outcome async.
    using EngineTester = std::function<void(const QString &engineId, std::function<void(bool, QString)> done)>;

    explicit SettingsPage(QWidget *parent = nullptr);

    // Provide the engine connectivity tester (see EngineTester). Optional; when
    // unset the test buttons report that testing is unavailable.
    void setEngineTester(EngineTester tester) { m_engineTester = std::move(tester); }

signals:
    // Any engines/* key was edited (enable, order, credentials, …). The app
    // debounces this into TranslateService::reloadEngines() so changes apply
    // without closing the page.
    void enginesChanged();

private:
    QWidget *buildGeneralPage();
    QWidget *buildHotkeysPage();
    QWidget *buildServicesPage();
    QWidget *buildOcrPage();
    QWidget *buildPlatformPage();

    // Move the service card at `index` one slot up/down and persist the new
    // engines/<id>/order values for every card.
    void moveService(int index, int delta);
    void persistServiceOrder();

    // Run a quick live translation through one engine and report success/failure
    // inline (connectivity test button, DESIGN.md §7).
    void testEngine(const QString &engineId, QLabel *resultLabel);

    EngineTester m_engineTester;

    // Services section: cards live in this layout in display order (the
    // trailing stretch stays last). Parallel list of engine ids per card.
    QVBoxLayout *m_servicesLayout = nullptr;
    QStringList m_serviceOrder;
};
