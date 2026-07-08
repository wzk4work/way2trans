#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class OcrBackend;

// Orchestrates OCR across the available backends (DESIGN.md §6).
//
// Builds the set of usable backends (WeChat when installed, Tesseract when
// compiled + language data present), ordered by quality: WeChat first when it
// is available, Tesseract as the reliable fallback (RapidOCR is M6). A recognise
// tries the preferred backend; if it fails, the service automatically retries
// the next available backend so a missing WeChat helper or a crash degrades to
// Tesseract instead of failing outright. Settings ([ocr] backend) can pin a
// preferred backend. Successful text is cleaned through TextCleaner before it is
// emitted, matching the translation pipeline (DESIGN.md §6).
class OcrService : public QObject {
    Q_OBJECT

public:
    explicit OcrService(QObject *parent = nullptr);

    // Backend ids available this session, in preference order.
    QStringList availableBackendIds() const;

    // True when at least one OCR backend is usable. When false, screenshot
    // translation is unavailable and the caller should surface unavailableHint().
    bool hasBackend() const;

    // Human-readable guidance when no backend is usable (e.g. "install
    // tesseract-ocr language packs"). Empty when a backend is available.
    QString unavailableHint() const;

    // Recognise text in `image`. langHints are BCP-47-ish ("en"/"zh"); empty
    // means auto. Emits recognized() on success (after cleaning) or failed()
    // once every available backend has been tried.
    void recognize(const QImage &image, const QStringList &langHints = {});

signals:
    void recognized(const QString &text);
    void failed(const QString &reason);

private:
    void buildBackends();
    void tryBackend(int index);

    QList<OcrBackend *> m_backends; // owned via QObject parent (this), in order

    // In-flight request state (one recognise at a time; a new one supersedes).
    QImage m_pendingImage;
    QStringList m_pendingHints;
    int m_currentIndex = -1;
    quint64 m_requestSeq = 0; // guards against stale backend signals
};
