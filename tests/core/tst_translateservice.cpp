#include "Settings.h"
#include "TranslateService.h"

#include <QTest>

// TranslateService engine-set assembly: enabled flags and the user-configured
// display order (engines/<id>/order, written by the settings page) must decide
// which engines load and in what order. Hermetic: only touches the XDG config
// the test harness points at the build tree; no network requests are made.
class TestTranslateService : public QObject {
    Q_OBJECT

private slots:
    void configuredOrderIsRespected();
    void disabledEnginesAreSkipped();
};

namespace {
void setEngineState(const QString &id, bool enabled, int order)
{
    Settings &s = Settings::instance();
    s.setValue(QStringLiteral("engines/%1/enabled").arg(id), enabled);
    s.setValue(QStringLiteral("engines/%1/order").arg(id), order);
}
} // namespace

void TestTranslateService::configuredOrderIsRespected()
{
    // Reverse of the registry declaration order (google, bing, deepl, llm).
    setEngineState(QStringLiteral("google"), true, 3);
    setEngineState(QStringLiteral("bing"), true, 2);
    setEngineState(QStringLiteral("deepl"), true, 1);
    setEngineState(QStringLiteral("llm"), true, 0);

    TranslateService service;
    const QStringList ids = service.activeEngineIds();
    QCOMPARE(ids,
        QStringList(
            {QStringLiteral("llm"), QStringLiteral("deepl"), QStringLiteral("bing"), QStringLiteral("google")}));
}

void TestTranslateService::disabledEnginesAreSkipped()
{
    setEngineState(QStringLiteral("google"), true, 0);
    setEngineState(QStringLiteral("bing"), false, 1);
    setEngineState(QStringLiteral("deepl"), false, 2);
    setEngineState(QStringLiteral("llm"), false, 3);

    TranslateService service;
    QCOMPARE(service.activeEngineIds(), QStringList({QStringLiteral("google")}));
}

QTEST_MAIN(TestTranslateService)
#include "tst_translateservice.moc"
