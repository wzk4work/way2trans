#include "CaptureOverlay.h"

#include <QImage>
#include <QSignalSpy>
#include <QTest>

// CaptureOverlay smoke test (DESIGN.md §13 M3: "instantiate/show without
// crashing"). Interactive drag-selection can't be scripted headlessly, so we
// verify the widget constructs, adopts the target geometry, and exposes the
// expected signals — plus that Esc emits cancelled().
class TestCaptureOverlay : public QObject {
    Q_OBJECT

private slots:
    void constructsAndTakesGeometry();
    void escapeEmitsCancelled();
};

void TestCaptureOverlay::constructsAndTakesGeometry()
{
    QImage frame(200, 120, QImage::Format_RGB32);
    frame.fill(Qt::gray);
    const QRect geom(0, 0, 200, 120);

    CaptureOverlay overlay(frame, geom);
    QCOMPARE(overlay.geometry(), geom);

    // Signals exist with the expected signatures (compile-time + meta check).
    QVERIFY(overlay.metaObject()->indexOfSignal("regionSelected(QImage,QRect)") >= 0);
    QVERIFY(overlay.metaObject()->indexOfSignal("cancelled()") >= 0);
}

void TestCaptureOverlay::escapeEmitsCancelled()
{
    QImage frame(200, 120, QImage::Format_RGB32);
    frame.fill(Qt::gray);
    // WA_DeleteOnClose deletes the overlay when Esc closes it; keep the spy on a
    // heap instance so the deletion during the key event is well-defined.
    auto *overlay = new CaptureOverlay(frame, QRect(0, 0, 200, 120));
    QSignalSpy cancelSpy(overlay, &CaptureOverlay::cancelled);

    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(overlay, &esc);

    QCOMPARE(cancelSpy.count(), 1);
}

QTEST_MAIN(TestCaptureOverlay)
#include "tst_captureoverlay.moc"
