#include "LlmEngine.h"

#include <QTest>

// Tests for the LLM engine's pure helpers: prompt expansion and SSE stream-line
// parsing. The live streaming call needs a real key/endpoint and cannot be
// exercised hermetically (DESIGN.md §5).
class TestLlmEngine : public QObject {
    Q_OBJECT

private slots:
    void expandsPlaceholders();
    void parsesStreamDelta();
    void accumulatesAcrossLines();
    void recognisesDoneSentinel();
    void ignoresNonDataLines();
};

void TestLlmEngine::expandsPlaceholders()
{
    TranslateRequest req;
    req.text = QStringLiteral("hello");
    req.sourceLang = QStringLiteral("en");
    req.targetLang = QStringLiteral("zh-CN");
    const QString out = LlmEngine::expandPrompt(QStringLiteral("From {source} to {target}: {text}"), req);
    QCOMPARE(out, QStringLiteral("From en to zh-CN: hello"));

    // "auto" source is spelled out for the model.
    req.sourceLang = QStringLiteral("auto");
    const QString out2 = LlmEngine::expandPrompt(QStringLiteral("{source}"), req);
    QCOMPARE(out2, QStringLiteral("the source language"));
}

void TestLlmEngine::parsesStreamDelta()
{
    QString delta;
    const bool done = LlmEngine::parseStreamLine(R"(data: {"choices":[{"delta":{"content":"你好"}}]})", &delta);
    QVERIFY(!done);
    QCOMPARE(delta, QStringLiteral("你好"));
}

void TestLlmEngine::accumulatesAcrossLines()
{
    QString delta;
    LlmEngine::parseStreamLine(R"(data: {"choices":[{"delta":{"content":"Hel"}}]})", &delta);
    LlmEngine::parseStreamLine(R"(data: {"choices":[{"delta":{"content":"lo"}}]})", &delta);
    QCOMPARE(delta, QStringLiteral("Hello"));
}

void TestLlmEngine::recognisesDoneSentinel()
{
    QString delta;
    QVERIFY(LlmEngine::parseStreamLine("data: [DONE]", &delta));
    QVERIFY(delta.isEmpty());
}

void TestLlmEngine::ignoresNonDataLines()
{
    QString delta;
    QVERIFY(!LlmEngine::parseStreamLine(": keep-alive comment", &delta));
    QVERIFY(!LlmEngine::parseStreamLine("", &delta));
    QVERIFY(delta.isEmpty());
}

QTEST_MAIN(TestLlmEngine)
#include "tst_llmengine.moc"
