#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QVBoxLayout;
struct ServiceInstance;
struct ServiceTemplate;

// Service management page (DESIGN.md §7), Bob/Pot model: an "Add service"
// menu of built-in templates plus one expandable card per configured service
// instance. Cards support renaming, enabling, reordering, deletion and a
// connectivity test; several instances of the same engine type can coexist
// (e.g. two OpenAI-compatible endpoints).
//
// Every control writes straight through to Settings ([services/<uuid>], see
// core/ServiceInstances); enginesChanged() lets the app hot-reload the engine
// set behind a debounce.
class ServicesPage : public QWidget {
    Q_OBJECT

public:
    // Callback the connectivity-test button uses to run a live translation
    // through one service instance. The app supplies it (it owns the engine
    // subsystem, so ui/ never links engines/). done(ok, message) is async.
    using EngineTester = std::function<void(const QString &instanceId, std::function<void(bool, QString)> done)>;

    explicit ServicesPage(QWidget *parent = nullptr);

    void setEngineTester(EngineTester tester) { m_engineTester = std::move(tester); }

signals:
    // The service list or a service's config changed; the app debounces this
    // into TranslateService::reloadEngines().
    void enginesChanged();

private:
    // Throw away and recreate all cards from the persisted instance list.
    // `expandId` re-opens one card (e.g. a freshly added service).
    void rebuild(const QString &expandId = {});
    QWidget *buildCard(const ServiceInstance &inst, bool expanded);

    void addService(const ServiceTemplate &tpl);
    void removeService(const QString &id, const QString &name);
    void moveService(const QString &id, int delta);

    void testService(const QString &id, QLabel *resultLabel);

    EngineTester m_engineTester;
    QVBoxLayout *m_cardsLayout = nullptr; // cards only, in display order
    QStringList m_order; // instance ids matching m_cardsLayout
};
