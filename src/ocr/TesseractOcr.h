#pragma once

#include "OcrBackend.h"

#include <QStringList>

// Tesseract OCR backend (DESIGN.md §6).
//
// Links libtesseract + leptonica directly. Recognition runs on the global
// QThreadPool so the UI thread never blocks on a multi-hundred-ms recognise.
// Preprocessing (2x upscale + grayscale + adaptive threshold) markedly improves
// recognition of small on-screen text.
//
// This whole backend is compiled only when WAY2TRANS_HAVE_TESSERACT is defined
// (CMake found libtesseract via pkg-config). When it is absent the file is not
// built at all and OcrService reports Tesseract as unavailable.
class TesseractOcr : public OcrBackend {
    Q_OBJECT

public:
    explicit TesseractOcr(QObject *parent = nullptr);
    ~TesseractOcr() override;

    QString id() const override;
    QString displayName() const override;
    void recognize(const QImage &image, const QStringList &langHints) override;

    // The tessdata language codes installed on this machine (e.g. "eng",
    // "chi_sim"). Empty if the tessdata directory could not be found. Used to
    // give a precise "install the chi_sim language pack" hint.
    static QStringList availableLanguages();

    // Map BCP-47-ish hints ("en", "zh", "zh-CN") to a Tesseract "+"-joined lang
    // string ("eng+chi_sim"), restricted to what is installed. Falls back to the
    // best installed default when hints don't resolve. Pure + unit-tested.
    static QString resolveLanguages(const QStringList &hints, const QStringList &installed);

private:
    // Runs on a QThreadPool thread: preprocess, run tesseract, emit via queued
    // signals back on this object's thread. `langs` is a resolved "+"-joined
    // Tesseract language string.
    void runRecognition(const QImage &image, const QString &langs);
};
