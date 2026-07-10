#include "DeepLEngine.h"

#include <QTest>

// Parser / routing tests for the DeepL engine. All pure functions, no network
// (DESIGN.md §5). The live API call needs a real key and cannot be exercised
// hermetically; the no-key fast-fail path is covered by the app-level flow.
class TestDeepLEngine : public QObject {
    Q_OBJECT

private slots:
    void routesFreeKeyToFreeHost();
    void routesProKeyToProHost();
    void mapsLanguages();
    void parsesTranslation();
    void parsesErrorMessage();
    void invalidBodyYieldsError();
};

void TestDeepLEngine::routesFreeKeyToFreeHost()
{
    QCOMPARE(DeepLEngine::hostForKey(QStringLiteral("abc-123:fx")), QStringLiteral("api-free.deepl.com"));
}

void TestDeepLEngine::routesProKeyToProHost()
{
    QCOMPARE(DeepLEngine::hostForKey(QStringLiteral("abc-123")), QStringLiteral("api.deepl.com"));
}

void TestDeepLEngine::mapsLanguages()
{
    // auto/empty source -> empty (omitted from the request).
    QVERIFY(DeepLEngine::toDeepLLang(QStringLiteral("auto"), false).isEmpty());
    QVERIFY(DeepLEngine::toDeepLLang(QString(), false).isEmpty());
    // English target keeps a region; source drops it.
    QCOMPARE(DeepLEngine::toDeepLLang(QStringLiteral("en"), true), QStringLiteral("EN-US"));
    QCOMPARE(DeepLEngine::toDeepLLang(QStringLiteral("en"), false), QStringLiteral("EN"));
    // Chinese: source is ZH, target is ZH-HANS.
    QCOMPARE(DeepLEngine::toDeepLLang(QStringLiteral("zh-CN"), true), QStringLiteral("ZH-HANS"));
    QCOMPARE(DeepLEngine::toDeepLLang(QStringLiteral("zh"), false), QStringLiteral("ZH"));
    QCOMPARE(DeepLEngine::toDeepLLang(QStringLiteral("zh-TW"), true), QStringLiteral("ZH-HANT"));
    // Generic code upper-cased and region-stripped.
    QCOMPARE(DeepLEngine::toDeepLLang(QStringLiteral("de"), true), QStringLiteral("DE"));
}

void TestDeepLEngine::parsesTranslation()
{
    const QByteArray body = R"({"translations":[{"detected_source_language":"EN","text":"你好世界"}]})";
    const TranslateResult r = DeepLEngine::parseResponse(body);
    QVERIFY(r.ok());
    QCOMPARE(r.translatedText, QStringLiteral("你好世界"));
    QCOMPARE(r.detectedSourceLang, QStringLiteral("en"));
}

void TestDeepLEngine::parsesErrorMessage()
{
    const QByteArray body = R"({"message":"Authorization failure, check auth_key"})";
    const TranslateResult r = DeepLEngine::parseResponse(body);
    QVERIFY(!r.ok());
    QVERIFY(r.error.contains(QStringLiteral("Authorization")));
}

void TestDeepLEngine::invalidBodyYieldsError()
{
    const TranslateResult r = DeepLEngine::parseResponse(QByteArrayLiteral("<html>nope</html>"));
    QVERIFY(!r.ok());
}

QTEST_MAIN(TestDeepLEngine)
#include "tst_deeplengine.moc"
