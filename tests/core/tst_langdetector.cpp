#include "LangDetector.h"

#include <QTest>

// Unit tests for the writing-system language heuristic (DESIGN.md §5.1). Only
// the coarse "Chinese vs. foreign" distinction and a few scripts matter for M1.
class TestLangDetector : public QObject {
    Q_OBJECT

private slots:
    void detectsChinese();
    void detectsEnglish();
    void detectsRussian();
    void detectsJapaneseOverKanji();
    void detectsKorean();
    void mixedCjkAndLatinIsChinese();
    void hanNoiseInLatinTextIsEnglish();
    void ambiguousIsAuto();
};

void TestLangDetector::detectsChinese()
{
    QCOMPARE(LangDetector::detectLang(QStringLiteral("你好世界")), QStringLiteral("zh"));
}

void TestLangDetector::detectsEnglish()
{
    QCOMPARE(LangDetector::detectLang(QStringLiteral("hello world")), QStringLiteral("en"));
}

void TestLangDetector::detectsRussian()
{
    QCOMPARE(LangDetector::detectLang(QStringLiteral("привет мир")), QStringLiteral("ru"));
}

void TestLangDetector::detectsJapaneseOverKanji()
{
    // Kana present -> Japanese even though kanji (Han) also appears.
    QCOMPARE(LangDetector::detectLang(QStringLiteral("これは日本語です")), QStringLiteral("ja"));
}

void TestLangDetector::detectsKorean()
{
    QCOMPARE(LangDetector::detectLang(QStringLiteral("안녕하세요")), QStringLiteral("ko"));
}

void TestLangDetector::mixedCjkAndLatinIsChinese()
{
    // A few Latin chars mixed into Chinese text is still Chinese for our rule.
    QCOMPARE(LangDetector::detectLang(QStringLiteral("使用 Qt 开发")), QStringLiteral("zh"));
}

void TestLangDetector::hanNoiseInLatinTextIsEnglish()
{
    // OCR of an English screenshot often misreads a dash or stray glyph as a
    // single Han character; that noise must not reclassify the paragraph.
    QString text
        = QStringLiteral("The project began as a coding harness built around developer workflows. ").repeated(3);
    text += QChar(0x4E00); // "一", a plausible OCR misread of a dash
    QCOMPARE(LangDetector::detectLang(text), QStringLiteral("en"));
}

void TestLangDetector::ambiguousIsAuto()
{
    QCOMPARE(LangDetector::detectLang(QStringLiteral("12345 !!!")), QStringLiteral("auto"));
}

QTEST_MAIN(TestLangDetector)
#include "tst_langdetector.moc"
