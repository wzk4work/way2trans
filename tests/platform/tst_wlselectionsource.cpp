#include "WlSelectionSource.h"

#include <QTest>

// Unit tests for the pure output-interpretation logic of WlSelectionSource.
// The actual wl-paste subprocess and Wayland compositor are NOT exercised here
// (this runs on any platform, including the X11 dev box); we test the decision
// table that maps a wl-paste run to a selection string.
class TestWlSelectionSource : public QObject {
    Q_OBJECT

private slots:
    void nonZeroExitIsEmptySelection();
    void successfulExitReturnsText();
    void trailingWhitespaceIsTrimmed();
    void emptyStdoutOnSuccessIsEmpty();
    void utf8IsDecoded();
};

void TestWlSelectionSource::nonZeroExitIsEmptySelection()
{
    // wl-paste exits non-zero when there is no selection to offer. Any stdout in
    // that case is irrelevant; the result must be an empty (not failed) selection.
    QCOMPARE(WlSelectionSource::interpretOutput(QByteArrayLiteral("garbage"), /*exitOk=*/false), QString());
    QCOMPARE(WlSelectionSource::interpretOutput(QByteArray(), /*exitOk=*/false), QString());
}

void TestWlSelectionSource::successfulExitReturnsText()
{
    QCOMPARE(WlSelectionSource::interpretOutput(QByteArrayLiteral("hello world"), /*exitOk=*/true),
        QStringLiteral("hello world"));
}

void TestWlSelectionSource::trailingWhitespaceIsTrimmed()
{
    // --no-newline drops one trailing newline; we trim defensively so callers see
    // a clean empty-string for a whitespace-only selection.
    QCOMPARE(WlSelectionSource::interpretOutput(QByteArrayLiteral("hi\n"), true), QStringLiteral("hi"));
    QCOMPARE(WlSelectionSource::interpretOutput(QByteArrayLiteral("  spaced  "), true), QStringLiteral("spaced"));
    QCOMPARE(WlSelectionSource::interpretOutput(QByteArrayLiteral("   \n\t "), true), QString());
}

void TestWlSelectionSource::emptyStdoutOnSuccessIsEmpty()
{
    QCOMPARE(WlSelectionSource::interpretOutput(QByteArray(), true), QString());
}

void TestWlSelectionSource::utf8IsDecoded()
{
    const QByteArray utf8 = QStringLiteral("翻译 café").toUtf8();
    QCOMPARE(WlSelectionSource::interpretOutput(utf8, true), QStringLiteral("翻译 café"));
}

QTEST_GUILESS_MAIN(TestWlSelectionSource)
#include "tst_wlselectionsource.moc"
