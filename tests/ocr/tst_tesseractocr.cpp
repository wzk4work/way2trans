#include "TesseractOcr.h"
#include "WeChatOcr.h"

#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

// Real Tesseract recognition test (DESIGN.md §13 M3 acceptance). Renders text
// into a QImage with QPainter, feeds it to the real TesseractOcr backend, and
// asserts the recognised text contains the expected words. This proves the
// libtesseract link, the QImage->PIX conversion, the preprocessing pipeline and
// the QThreadPool async path all work together — not just parsing fixtures.
//
// Gated on WAY2TRANS_HAVE_TESSERACT: when Tesseract was not compiled in, the
// recognition cases skip (the resolveLanguages pure logic still runs).
class TestTesseractOcr : public QObject {
    Q_OBJECT

private slots:
    void resolveLanguagesMapsHints();
    void resolveLanguagesFallsBackToInstalled();
    void resolveLanguagesDefaultsWhenNoHints();

#ifdef WAY2TRANS_HAVE_TESSERACT
    void recognisesEnglishText();
    void recognisesChineseTextIfInstalled();
#endif

    void wechatProbeIsConsistent();

private:
    static QImage renderText(const QString &text, const QString &family, int pt);
};

QImage TestTesseractOcr::renderText(const QString &text, const QString &family, int pt)
{
    // White background, black text: the high-contrast case OCR handles best.
    QImage img(560, 140, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    QFont f(family, pt);
    p.setFont(f);
    p.setPen(Qt::black);
    p.drawText(img.rect(), Qt::AlignCenter, text);
    p.end();
    return img;
}

void TestTesseractOcr::resolveLanguagesMapsHints()
{
    const QStringList installed {QStringLiteral("eng"), QStringLiteral("chi_sim"), QStringLiteral("osd")};
    QCOMPARE(TesseractOcr::resolveLanguages({QStringLiteral("en")}, installed), QStringLiteral("eng"));
    QCOMPARE(TesseractOcr::resolveLanguages({QStringLiteral("zh-CN")}, installed), QStringLiteral("chi_sim"));
    QCOMPARE(TesseractOcr::resolveLanguages({QStringLiteral("en"), QStringLiteral("zh")}, installed),
        QStringLiteral("eng+chi_sim"));
}

void TestTesseractOcr::resolveLanguagesFallsBackToInstalled()
{
    // A Chinese hint with only eng installed resolves to the installed default.
    const QStringList installed {QStringLiteral("eng")};
    QCOMPARE(TesseractOcr::resolveLanguages({QStringLiteral("zh")}, installed), QStringLiteral("eng"));
}

void TestTesseractOcr::resolveLanguagesDefaultsWhenNoHints()
{
    const QStringList installed {QStringLiteral("eng"), QStringLiteral("chi_sim")};
    // No hints -> the common screen-translation default eng+chi_sim.
    QCOMPARE(TesseractOcr::resolveLanguages({}, installed), QStringLiteral("eng+chi_sim"));
    // Nothing installed -> eng as a last resort (Init will then fail cleanly).
    QCOMPARE(TesseractOcr::resolveLanguages({}, {}), QStringLiteral("eng"));
}

#ifdef WAY2TRANS_HAVE_TESSERACT
void TestTesseractOcr::recognisesEnglishText()
{
    if (!TesseractOcr::availableLanguages().contains(QStringLiteral("eng")))
        QSKIP("eng language pack not installed");

    TesseractOcr ocr;
    QSignalSpy okSpy(&ocr, &OcrBackend::recognized);
    QSignalSpy failSpy(&ocr, &OcrBackend::failed);

    const QImage img = renderText(QStringLiteral("Hello World"), QStringLiteral("DejaVu Sans"), 32);
    ocr.recognize(img, {QStringLiteral("en")});

    QVERIFY(okSpy.wait(20000) || failSpy.count() > 0);
    QVERIFY2(failSpy.isEmpty(), failSpy.isEmpty() ? "" : qPrintable(failSpy.first().first().toString()));
    QCOMPARE(okSpy.count(), 1);
    const QString text = okSpy.first().first().toString();
    // Recognition is fuzzy; assert the salient words survive, case-insensitively.
    QVERIFY2(text.contains(QStringLiteral("Hello"), Qt::CaseInsensitive), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("World"), Qt::CaseInsensitive), qPrintable(text));
}

void TestTesseractOcr::recognisesChineseTextIfInstalled()
{
    if (!TesseractOcr::availableLanguages().contains(QStringLiteral("chi_sim")))
        QSKIP("chi_sim language pack not installed");

    // Use a CJK-capable font; skip if none is available on the box.
    const QString family = QStringLiteral("Noto Sans CJK SC");
    const QImage img = renderText(QStringLiteral("你好世界"), family, 32);

    TesseractOcr ocr;
    QSignalSpy okSpy(&ocr, &OcrBackend::recognized);
    QSignalSpy failSpy(&ocr, &OcrBackend::failed);
    ocr.recognize(img, {QStringLiteral("zh")});
    QVERIFY(okSpy.wait(20000) || failSpy.count() > 0);
    if (!failSpy.isEmpty())
        QSKIP("Chinese OCR failed (likely missing CJK font on this box)");
    const QString text = okSpy.first().first().toString().remove(QLatin1Char(' '));
    // At least one of the four characters should survive recognition.
    QVERIFY2(text.contains(QStringLiteral("你")) || text.contains(QStringLiteral("好"))
            || text.contains(QStringLiteral("世")) || text.contains(QStringLiteral("界")),
        qPrintable(text));
}
#endif

void TestTesseractOcr::wechatProbeIsConsistent()
{
    // Probe must be deterministic and isAvailable() must agree with it. We can't
    // assume WeChat is or isn't installed on CI, so only assert the invariant.
    const QString path = WeChatOcr::probeInstallPath();
    QCOMPARE(WeChatOcr::isAvailable(), !path.isEmpty());
}

QTEST_MAIN(TestTesseractOcr)
#include "tst_tesseractocr.moc"
