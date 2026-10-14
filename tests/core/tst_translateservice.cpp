#include "ServiceInstances.h"
#include "Settings.h"
#include "TranslateService.h"

#include <QTest>

// TranslateService engine-set assembly over the service-instance model
// (Bob/Pot style, DESIGN.md §5.1): legacy [engines/<type>] configs migrate
// into instances once, several instances of one engine type can coexist, and
// enabled flags plus the persisted order decide what loads and in what order.
// Hermetic: the test harness points XDG_CONFIG_HOME at the build tree; no
// network requests are made.
class TestTranslateService : public QObject {
    Q_OBJECT

private slots:
    void init(); // wipe services + legacy engine keys before each test
    void migratesLegacyEngineConfig();
    void multipleInstancesOfSameType();
    void disabledInstancesAreSkipped();
    void orderIsRespected();
};

void TestTranslateService::init()
{
    Settings &s = Settings::instance();
    s.remove(QStringLiteral("services"));
    s.remove(QStringLiteral("engines"));
}

void TestTranslateService::migratesLegacyEngineConfig()
{
    Settings &s = Settings::instance();
    // Legacy layout: LLM configured+enabled and ordered first, google off.
    s.setValue(QStringLiteral("engines/llm/enabled"), true);
    s.setValue(QStringLiteral("engines/llm/apiKey"), QStringLiteral("legacy-key"));
    s.setValue(QStringLiteral("engines/llm/order"), 0);
    s.setValue(QStringLiteral("engines/google/enabled"), false);
    s.setValue(QStringLiteral("engines/google/order"), 1);

    TranslateService service; // constructor seeds + loads
    const QList<ServiceInstance> instances = ServiceInstances::all();
    QCOMPARE(instances.size(), 4); // one per legacy engine type

    // The LLM instance comes first and carried its key over.
    QCOMPARE(instances.first().engineType, QStringLiteral("llm"));
    const QString migratedKey
        = Settings::instance()
              .value(ServiceInstances::configGroup(instances.first().id) + QStringLiteral("/apiKey"))
              .toString();
    QCOMPARE(migratedKey, QStringLiteral("legacy-key"));

    // Active set respects the legacy enabled flags: llm on, google off,
    // bing on by default, deepl off by default.
    const QStringList ids = service.activeEngineIds();
    QCOMPARE(ids.size(), 2);
    QCOMPARE(ids.first(), instances.first().id);

    // Seeding must not repeat: a second service sees the same four instances.
    TranslateService again;
    QCOMPARE(ServiceInstances::all().size(), 4);
}

void TestTranslateService::multipleInstancesOfSameType()
{
    Settings::instance().setValue(QStringLiteral("services/seeded"), true); // suppress legacy seeding

    const QString a = ServiceInstances::add(*ServiceInstances::templateById(QStringLiteral("llm-openai")));
    const QString b = ServiceInstances::add(*ServiceInstances::templateById(QStringLiteral("llm-deepseek")));
    ServiceInstances::setEnabled(a, true);
    ServiceInstances::setEnabled(b, true);

    TranslateService service;
    QCOMPARE(service.activeEngineIds(), QStringList({a, b}));
    // Two instances of the same engine type keep their own names and configs.
    QCOMPARE(service.engineDisplayName(a), QStringLiteral("OpenAI"));
    QCOMPARE(service.engineDisplayName(b), QStringLiteral("DeepSeek"));
    const QString endpointB
        = Settings::instance().value(ServiceInstances::configGroup(b) + QStringLiteral("/endpoint")).toString();
    QCOMPARE(endpointB, QStringLiteral("https://api.deepseek.com/v1"));
}

void TestTranslateService::disabledInstancesAreSkipped()
{
    Settings::instance().setValue(QStringLiteral("services/seeded"), true);

    const QString a = ServiceInstances::add(*ServiceInstances::templateById(QStringLiteral("google")));
    const QString b = ServiceInstances::add(*ServiceInstances::templateById(QStringLiteral("bing")));
    ServiceInstances::setEnabled(b, false);

    TranslateService service;
    QCOMPARE(service.activeEngineIds(), QStringList({a}));
}

void TestTranslateService::orderIsRespected()
{
    Settings::instance().setValue(QStringLiteral("services/seeded"), true);

    const QString a = ServiceInstances::add(*ServiceInstances::templateById(QStringLiteral("google")));
    const QString b = ServiceInstances::add(*ServiceInstances::templateById(QStringLiteral("bing")));
    ServiceInstances::setOrder({b, a});

    TranslateService service;
    QCOMPARE(service.activeEngineIds(), QStringList({b, a}));
}

QTEST_MAIN(TestTranslateService)
#include "tst_translateservice.moc"
