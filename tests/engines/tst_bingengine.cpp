#include "BingWebEngine.h"

#include <QTest>

// Parser tests for the Bing translator: token scraping from the landing page and
// the ttranslatev3 response. Embedded fixtures, no network (DESIGN.md §5.1).
class TestBingEngine : public QObject {
    Q_OBJECT

private slots:
    void parsesTokenFromPage();
    void tokenMissingIsInvalid();
    void parsesTranslationArray();
    void parsesErrorObject();
    void invalidBodyYieldsError();
};

void TestBingEngine::parsesTokenFromPage()
{
    // Minimal HTML carrying the three token fragments the engine scrapes.
    const QByteArray html = R"(
      <html><head><script>
        var _G = { IG:"ABC123DEF456", ... };
        params_AbusePrevention = [1699999999999,"tok-en-string",3600000];
      </script></head>
      <body><div id="rich_tta" data-iid="translator.5023"></div></body></html>
    )";
    const BingWebEngine::AuthToken token = BingWebEngine::parseToken(html);
    QVERIFY(token.isValid());
    QCOMPARE(token.ig, QStringLiteral("ABC123DEF456"));
    QCOMPARE(token.iid, QStringLiteral("translator.5023.2"));
    QCOMPARE(token.key, QStringLiteral("1699999999999"));
    QCOMPARE(token.token, QStringLiteral("tok-en-string"));
    QCOMPARE(token.expiryMs, 3600000);
}

void TestBingEngine::tokenMissingIsInvalid()
{
    const BingWebEngine::AuthToken token = BingWebEngine::parseToken(QByteArrayLiteral("<html></html>"));
    QVERIFY(!token.isValid());
}

void TestBingEngine::parsesTranslationArray()
{
    const QByteArray body
        = R"([{"detectedLanguage":{"language":"en","score":1.0},"translations":[{"text":"你好","to":"zh-Hans"}]}])";
    const TranslateResult r = BingWebEngine::parseResponse(body);
    QVERIFY(r.ok());
    QCOMPARE(r.translatedText, QStringLiteral("你好"));
    QCOMPARE(r.detectedSourceLang, QStringLiteral("en"));
}

void TestBingEngine::parsesErrorObject()
{
    const QByteArray body = R"({"statusCode":400,"errorMessage":"token expired"})";
    const TranslateResult r = BingWebEngine::parseResponse(body);
    QVERIFY(!r.ok());
    QCOMPARE(r.error, QStringLiteral("token expired"));
}

void TestBingEngine::invalidBodyYieldsError()
{
    const TranslateResult r = BingWebEngine::parseResponse(QByteArrayLiteral("not json"));
    QVERIFY(!r.ok());
}

QTEST_MAIN(TestBingEngine)
#include "tst_bingengine.moc"
