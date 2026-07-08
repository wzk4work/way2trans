#include "TextCleaner.h"

#include <QTest>

// Unit tests for the hard-wrap merging / whitespace normalisation used before
// translation (DESIGN.md §3.1).
class TestTextCleaner : public QObject {
    Q_OBJECT

private slots:
    void mergesHardWrappedLines();
    void keepsParagraphBreakOnTerminator();
    void keepsBreakWhenNextLineStartsUpper();
    void collapsesRedundantWhitespace();
    void disabledModeKeepsNewlines();
    void trimsEdges();
};

void TestTextCleaner::mergesHardWrappedLines()
{
    // Line ends without punctuation and next line starts lowercase -> one line.
    const QString in = QStringLiteral("the quick brown fox\njumps over the lazy dog");
    QCOMPARE(TextCleaner::clean(in), QStringLiteral("the quick brown fox jumps over the lazy dog"));
}

void TestTextCleaner::keepsParagraphBreakOnTerminator()
{
    // A sentence terminator means a real paragraph break; keep the newline.
    const QString in = QStringLiteral("First sentence.\nsecond starts here");
    QCOMPARE(TextCleaner::clean(in), QStringLiteral("First sentence.\nsecond starts here"));
}

void TestTextCleaner::keepsBreakWhenNextLineStartsUpper()
{
    // Next line starts uppercase -> likely a new sentence, keep the break.
    const QString in = QStringLiteral("A heading\nNew paragraph body");
    QCOMPARE(TextCleaner::clean(in), QStringLiteral("A heading\nNew paragraph body"));
}

void TestTextCleaner::collapsesRedundantWhitespace()
{
    const QString in = QStringLiteral("too    many     spaces");
    QCOMPARE(TextCleaner::clean(in), QStringLiteral("too many spaces"));
}

void TestTextCleaner::disabledModeKeepsNewlines()
{
    const QString in = QStringLiteral("keep this\nline break");
    // With merging off, the newline survives (only spaces collapse).
    QCOMPARE(TextCleaner::clean(in, /*mergeHardWraps=*/false), QStringLiteral("keep this\nline break"));
}

void TestTextCleaner::trimsEdges()
{
    const QString in = QStringLiteral("   padded   ");
    QCOMPARE(TextCleaner::clean(in), QStringLiteral("padded"));
}

QTEST_MAIN(TestTextCleaner)
#include "tst_textcleaner.moc"
