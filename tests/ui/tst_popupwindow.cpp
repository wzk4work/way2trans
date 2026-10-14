#include "PopupWindow.h"
#include "Settings.h"
#include "engines/TranslateTypes.h"

#include <QFile>
#include <QFrame>
#include <QMouseEvent>
#include <QTest>

// Drag-to-resize behaviour of the result popup (DESIGN.md §7). The sizing is
// asymmetric by design: a dragged width is remembered across popups and
// persisted to popup/width, while a dragged height only overrides the popup
// it was dragged on — the next request goes back to the content-derived auto
// height. The harness points XDG_CONFIG_HOME at the build tree (hermetic).
class TestPopupWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cornerDragResizesThisPopup();
    void heightGoesBackToAutoOnNextRequest();
    void widthIsRememberedAndPersistedHeightIsNot();

private:
    void beginSampleRequest(PopupWindow &popup);
    void dragCard(PopupWindow &popup, const QPoint &gripOffsetFromCardBottomRight, const QPoint &delta);
    void sendMouse(QWidget *target, QEvent::Type type, const QPoint &globalPos, Qt::MouseButtons buttons);
};

void TestPopupWindow::initTestCase()
{
    // Earlier runs persist popup/width into the build-tree config; start
    // clean so the auto-size baseline really is automatic.
    QFile::remove(Settings::configFilePath());
}

void TestPopupWindow::beginSampleRequest(PopupWindow &popup)
{
    TranslateRequest request;
    request.text = QStringLiteral("hello world");
    request.sourceLang = QStringLiteral("en");
    request.targetLang = QStringLiteral("zh");
    popup.beginRequest(request, {QStringLiteral("g")}, {{QStringLiteral("g"), QStringLiteral("Google")}});
}

void TestPopupWindow::sendMouse(QWidget *target, QEvent::Type type, const QPoint &globalPos, Qt::MouseButtons buttons)
{
    const QPointF local = target->mapFromGlobal(globalPos);
    QMouseEvent event(type, local, QPointF(globalPos), Qt::LeftButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

// Press on the card `gripOffsetFromCardBottomRight` away from the card's
// bottom-right corner, drag by `delta`, release.
void TestPopupWindow::dragCard(PopupWindow &popup, const QPoint &gripOffsetFromCardBottomRight, const QPoint &delta)
{
    QFrame *card = popup.findChild<QFrame *>(QStringLiteral("popupCard"));
    QVERIFY(card);
    const QPoint grip = card->mapToGlobal(QPoint(card->width(), card->height()) + gripOffsetFromCardBottomRight);
    sendMouse(card, QEvent::MouseButtonPress, grip, Qt::LeftButton);
    sendMouse(card, QEvent::MouseMove, grip + delta, Qt::LeftButton);
    sendMouse(card, QEvent::MouseButtonRelease, grip + delta, Qt::NoButton);
}

void TestPopupWindow::cornerDragResizesThisPopup()
{
    PopupWindow popup;
    beginSampleRequest(popup);
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));
    QTest::qWait(50); // let the deferred adjustPopupSize() settle

    const QSize before = popup.size();
    dragCard(popup, QPoint(-2, -2), QPoint(80, 60)); // bottom-right corner
    QCOMPARE(popup.size(), before + QSize(80, 60));
}

void TestPopupWindow::heightGoesBackToAutoOnNextRequest()
{
    PopupWindow popup;
    beginSampleRequest(popup);
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));
    QTest::qWait(50);

    // Baseline in the same regime as the assertion below: the pre-show pass
    // estimates the chrome from hints, a visible re-request measures it from
    // live geometry, and the two differ by a few pixels.
    beginSampleRequest(popup);
    QTest::qWait(80);
    const QSize before = popup.size();

    dragCard(popup, QPoint(-2, -2), QPoint(80, 150)); // grow both dimensions
    QCOMPARE(popup.height(), before.height() + 150); // override holds for now

    // A new request keeps the dragged width but recomputes the height from
    // the content; wider means fewer wraps, so it can't exceed the old auto
    // height, let alone the dragged one.
    beginSampleRequest(popup);
    QTest::qWait(80);
    QCOMPARE(popup.width(), before.width() + 80);
    QVERIFY2(popup.height() <= before.height(),
        qPrintable(
            QStringLiteral("height %1 should have returned to auto (<= %2)").arg(popup.height()).arg(before.height())));
}

void TestPopupWindow::widthIsRememberedAndPersistedHeightIsNot()
{
    int draggedWidth = 0;
    int autoHeight = 0;
    {
        PopupWindow popup;
        beginSampleRequest(popup);
        popup.show();
        QVERIFY(QTest::qWaitForWindowExposed(&popup));
        QTest::qWait(50);
        autoHeight = popup.height();

        // Width-only drag on the right edge: the auto height is recomputed
        // immediately (same short content, so it stays put), no override.
        dragCard(popup, QPoint(-2, -popup.height() / 3), QPoint(90, 0));
        draggedWidth = popup.width();
        QCOMPARE(draggedWidth, popup.sizeHint().width()); // placement sees it too

        QTest::qWait(600); // debounced Settings write (400ms)
        QCOMPARE(Settings::instance().value(QStringLiteral("popup/width")).toInt(), draggedWidth);
        QVERIFY(!Settings::instance().value(QStringLiteral("popup/height")).isValid());
    }

    // A fresh popup applies the persisted width from the start, but derives
    // its height from the content as usual.
    PopupWindow fresh;
    beginSampleRequest(fresh);
    fresh.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fresh));
    QTest::qWait(80);
    QCOMPARE(fresh.width(), draggedWidth);
    QVERIFY(fresh.height() <= autoHeight);
}

QTEST_MAIN(TestPopupWindow)
#include "tst_popupwindow.moc"
