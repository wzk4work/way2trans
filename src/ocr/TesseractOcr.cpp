#include "TesseractOcr.h"

#include "Logging.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include <allheaders.h> // leptonica
#include <tesseract/baseapi.h>

namespace {

// Resolve the tessdata directory. Honour TESSDATA_PREFIX, then probe the common
// system locations. Returns an empty string if none contains an *.traineddata.
QString tessdataDir()
{
    QStringList candidates;
    const QByteArray envPrefix = qgetenv("TESSDATA_PREFIX");
    if (!envPrefix.isEmpty())
        candidates << QString::fromLocal8Bit(envPrefix);
    candidates << QStringLiteral("/usr/share/tesseract-ocr/5/tessdata")
               << QStringLiteral("/usr/share/tesseract-ocr/4.00/tessdata")
               << QStringLiteral("/usr/share/tessdata") << QStringLiteral("/usr/local/share/tessdata");
    for (const QString &dir : std::as_const(candidates)) {
        QDir d(dir);
        if (d.exists() && !d.entryList({QStringLiteral("*.traineddata")}, QDir::Files).isEmpty())
            return dir;
    }
    return {};
}

// Build a leptonica 32bpp PIX from a QImage. We normalise to RGBA8888 first so
// the byte layout is well-defined regardless of the source format/endianness,
// then set each pixel via the leptonica composer macro (which handles the PIX
// word packing). Correctness over speed: capture images are modest in size.
Pix *qImageToPix(const QImage &src)
{
    const QImage img = src.convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0)
        return nullptr;

    Pix *pix = pixCreate(w, h, 32);
    if (!pix)
        return nullptr;
    pixSetSpp(pix, 4);
    // Report a sane resolution so tesseract does not warn about dpi.
    pixSetResolution(pix, 96, 96);

    for (int y = 0; y < h; ++y) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const uchar r = line[x * 4 + 0];
            const uchar g = line[x * 4 + 1];
            const uchar b = line[x * 4 + 2];
            const uchar a = line[x * 4 + 3];
            l_uint32 pixel = 0;
            composeRGBAPixel(r, g, b, a, &pixel);
            pixSetPixel(pix, x, y, pixel);
        }
    }
    return pix;
}

// Preprocessing (DESIGN.md §6): 2x upscale + grayscale + adaptive (Otsu)
// binarisation. Small on-screen fonts recognise far better after this. Returns
// a new PIX (caller destroys) or nullptr on failure.
Pix *preprocess(Pix *rgba)
{
    if (!rgba)
        return nullptr;
    // 2x upscale on the colour image, then to gray, then adaptive threshold.
    Pix *scaled = pixScale(rgba, 2.0f, 2.0f);
    Pix *work = scaled ? scaled : rgba;

    Pix *gray = pixConvertRGBToGray(work, 0.0f, 0.0f, 0.0f); // luma weights
    if (scaled)
        pixDestroy(&scaled);
    if (!gray)
        return nullptr;

    Pix *binary = nullptr;
    // Tiled Otsu: sx/sy = tile size, scorefract balances fg/bg. Adaptive keeps
    // uneven backgrounds (gradients, shadows) from swamping the text.
    if (pixOtsuAdaptiveThreshold(gray, 200, 200, 0, 0, 0.1f, nullptr, &binary) != 0 || !binary) {
        // Fall back to the grayscale image if thresholding failed; tesseract can
        // still work on gray.
        return gray;
    }
    pixDestroy(&gray);
    return binary;
}

} // namespace

TesseractOcr::TesseractOcr(QObject *parent)
    : OcrBackend(parent)
{
}

TesseractOcr::~TesseractOcr() = default;

QString TesseractOcr::id() const
{
    return QStringLiteral("tesseract");
}

QString TesseractOcr::displayName() const
{
    return QStringLiteral("Tesseract");
}

QStringList TesseractOcr::availableLanguages()
{
    const QString dir = tessdataDir();
    if (dir.isEmpty())
        return {};
    QStringList langs;
    const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.traineddata")}, QDir::Files);
    for (const QFileInfo &fi : files)
        langs << fi.completeBaseName();
    return langs;
}

QString TesseractOcr::resolveLanguages(const QStringList &hints, const QStringList &installed)
{
    // Map hint prefixes to tessdata codes.
    QStringList wanted;
    for (const QString &hint : hints) {
        const QString h = hint.toLower();
        if (h.startsWith(QLatin1String("zh")))
            wanted << QStringLiteral("chi_sim");
        else if (h.startsWith(QLatin1String("en")))
            wanted << QStringLiteral("eng");
        else if (h.startsWith(QLatin1String("ja")))
            wanted << QStringLiteral("jpn");
        else if (h.startsWith(QLatin1String("ko")))
            wanted << QStringLiteral("kor");
    }

    // Keep only installed languages, preserving order and de-duplicating.
    QStringList resolved;
    for (const QString &code : std::as_const(wanted)) {
        if (installed.contains(code) && !resolved.contains(code))
            resolved << code;
    }

    // No usable hints: default to eng+chi_sim when installed (the common screen
    // translation case), otherwise whatever is installed, otherwise eng.
    if (resolved.isEmpty()) {
        for (const QString &code : {QStringLiteral("eng"), QStringLiteral("chi_sim")}) {
            if (installed.contains(code))
                resolved << code;
        }
        if (resolved.isEmpty() && !installed.isEmpty())
            resolved << installed.first();
        if (resolved.isEmpty())
            resolved << QStringLiteral("eng");
    }
    return resolved.join(QLatin1Char('+'));
}

void TesseractOcr::recognize(const QImage &image, const QStringList &langHints)
{
    if (image.isNull()) {
        emit failed(tr("No image to recognise"));
        return;
    }

    const QStringList installed = availableLanguages();
    if (installed.isEmpty()) {
        emit failed(tr("Tesseract language data not found. Install the tesseract-ocr language packs "
                       "(e.g. tesseract-ocr-eng, tesseract-ocr-chi-sim)."));
        return;
    }

    const QString langs = resolveLanguages(langHints, installed);
    // Guard: if a Chinese hint was given but chi_sim is missing, be explicit.
    const bool wantedChinese
        = std::any_of(langHints.cbegin(), langHints.cend(), [](const QString &h) { return h.toLower().startsWith(QLatin1String("zh")); });
    if (wantedChinese && !installed.contains(QStringLiteral("chi_sim"))) {
        qCWarning(w2tCore) << "OCR: chi_sim requested but not installed; using" << langs;
    }

    // Deep copy the image so it stays valid on the worker thread independent of
    // the caller's buffer lifetime.
    const QImage copy = image.copy();
    QThreadPool::globalInstance()->start([this, copy, langs]() { runRecognition(copy, langs); });
}

void TesseractOcr::runRecognition(const QImage &image, const QString &langs)
{
    // Emit results back on this object's own thread via a guarded pointer so a
    // recognise outliving the backend can't touch a dead object.
    QPointer<TesseractOcr> self(this);
    auto emitFailed = [self](const QString &reason) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, reason]() {
            if (self)
                emit self->failed(reason);
        }, Qt::QueuedConnection);
    };
    auto emitOk = [self](const QString &text, const QList<OcrLine> &lines) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, text, lines]() {
            if (!self)
                return;
            emit self->recognizedLines(lines);
            emit self->recognized(text);
        }, Qt::QueuedConnection);
    };

    const QString dir = tessdataDir();
    tesseract::TessBaseAPI api;
    const QByteArray dirUtf8 = dir.toUtf8();
    const QByteArray langsUtf8 = langs.toUtf8();
    if (api.Init(dir.isEmpty() ? nullptr : dirUtf8.constData(), langsUtf8.constData()) != 0) {
        emitFailed(tr("Failed to initialise Tesseract with languages: %1").arg(langs));
        return;
    }

    Pix *rgba = qImageToPix(image);
    if (!rgba) {
        api.End();
        emitFailed(tr("Could not convert the image for OCR"));
        return;
    }
    Pix *processed = preprocess(rgba);
    pixDestroy(&rgba);
    if (!processed) {
        api.End();
        emitFailed(tr("Image preprocessing failed"));
        return;
    }

    api.SetImage(processed);
    api.Recognize(nullptr);

    // Collect per-line text + boxes for later "translation in place" (DESIGN.md
    // §6.1). Concatenate the line text for the plain result.
    QList<OcrLine> lines;
    QString allText;
    if (tesseract::ResultIterator *it = api.GetIterator()) {
        const tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;
        do {
            char *lineText = it->GetUTF8Text(level);
            if (!lineText)
                continue;
            QString qline = QString::fromUtf8(lineText).trimmed();
            delete[] lineText;
            if (qline.isEmpty())
                continue;
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            it->BoundingBox(level, &x1, &y1, &x2, &y2);
            // Boxes are in the preprocessed (2x) image space; halve back to the
            // original image coordinates.
            OcrLine ol;
            ol.text = qline;
            ol.box = QRect(QPoint(x1 / 2, y1 / 2), QPoint(x2 / 2, y2 / 2));
            lines << ol;
            if (!allText.isEmpty())
                allText += QLatin1Char('\n');
            allText += qline;
        } while (it->Next(level));
        delete it;
    }

    pixDestroy(&processed);
    api.End();

    if (allText.trimmed().isEmpty()) {
        emitFailed(tr("No text recognised in the image"));
        return;
    }
    emitOk(allText, lines);
}
