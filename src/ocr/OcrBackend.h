#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

// A recognised text line with its bounding box in the source image (DESIGN.md
// §6.1). Bounding boxes are collected so later milestones can overlay a
// translation in place; the plain concatenated text is what the OCR service
// feeds to the translator today.
struct OcrLine {
    QString text;
    QRect box; // in source-image pixel coordinates
};

// Optical character recognition backend (DESIGN.md §6).
//
// Asynchronous and signal-based like the platform capabilities. recognize()
// returns immediately; the result arrives on recognized() (full plain text) or
// failed(). Implementations run the actual recognition off the UI thread
// (QThreadPool for Tesseract, a helper process for WeChat) so a slow recognise
// never blocks the event loop.
class OcrBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~OcrBackend() override = default;

    // Stable id ("tesseract" / "wechat") for settings + logs.
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    // Recognise text in `image`. langHints are BCP-47-ish hints ("en", "zh") the
    // backend maps to its own language packs; an empty list means "auto".
    virtual void recognize(const QImage &image, const QStringList &langHints) = 0;

signals:
    void recognized(const QString &text);
    // Emitted alongside recognized() when the backend produced line boxes.
    void recognizedLines(const QList<OcrLine> &lines);
    void failed(const QString &reason);
};
