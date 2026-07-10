#include "GoogleWebEngine.h"

#include <QTest>

// Parser tests for the Google translate_a/single response. Uses embedded JSON
// fixtures (real endpoint shape) so no network is touched (DESIGN.md §5.1).
class TestGoogleEngine : public QObject {
    Q_OBJECT

private slots:
    void parsesSentenceTranslation();
    void parsesDictionaryEntryForWord();
    void parsesRomanisationPhonetic();
    void invalidBodyYieldsError();
};

void TestGoogleEngine::parsesSentenceTranslation()
{
    // Shape: [[["你好","hello",null,null,10]],null,"en",...]
    const QByteArray body = R"([[["你好","hello",null,null,10]],null,"en",null,null,null,1.0,[]])";
    const TranslateResult r = GoogleWebEngine::parseResponse(body);
    QVERIFY(r.ok());
    QCOMPARE(r.translatedText, QStringLiteral("你好"));
    QCOMPARE(r.detectedSourceLang, QStringLiteral("en"));
}

void TestGoogleEngine::parsesDictionaryEntryForWord()
{
    // Single-word "book" -> dictionary block at root[1].
    const QByteArray body = R"([
      [["书","book",null,null,3]],
      [["noun",["书","书本","著作"],[["书",["book"]]],"book",1],
       ["verb",["预订","登记"],[["预订",["book"]]],"book",3]],
      "en"
    ])";
    const TranslateResult r = GoogleWebEngine::parseResponse(body);
    QVERIFY(r.ok());
    QVERIFY(r.dict.has_value());
    QCOMPARE(r.dict->meanings.size(), 2);
    QCOMPARE(r.dict->meanings.at(0).partOfSpeech, QStringLiteral("noun"));
    QCOMPARE(r.dict->meanings.at(0).definitions.size(), 3);
    QCOMPARE(r.dict->meanings.at(0).definitions.at(0), QStringLiteral("书"));
    QCOMPARE(r.dict->meanings.at(1).partOfSpeech, QStringLiteral("verb"));
}

void TestGoogleEngine::parsesRomanisationPhonetic()
{
    // A trailing [null,null,null,"nǐ hǎo"] entry carries the source romanisation.
    const QByteArray body
        = R"([[["hello","你好",null,null,10],[null,null,null,"nǐ hǎo"]],[["noun",["greeting"],[],"hello",1]],"zh-CN"])";
    const TranslateResult r = GoogleWebEngine::parseResponse(body);
    QVERIFY(r.ok());
    QCOMPARE(r.translatedText, QStringLiteral("hello"));
    QVERIFY(r.dict.has_value());
    QCOMPARE(r.dict->phonetic, QStringLiteral("nǐ hǎo"));
}

void TestGoogleEngine::invalidBodyYieldsError()
{
    const TranslateResult r = GoogleWebEngine::parseResponse(QByteArrayLiteral("<html>nope</html>"));
    QVERIFY(!r.ok());
}

QTEST_MAIN(TestGoogleEngine)
#include "tst_googleengine.moc"
