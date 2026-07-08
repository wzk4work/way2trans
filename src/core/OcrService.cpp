#include "OcrService.h"

#include "Logging.h"
#include "Settings.h"
#include "TextCleaner.h"
#include "ocr/OcrBackend.h"
#include "ocr/WeChatOcr.h"

#ifdef WAY2TRANS_HAVE_TESSERACT
#include "ocr/TesseractOcr.h"
#endif

OcrService::OcrService(QObject *parent)
    : QObject(parent)
{
    buildBackends();
}

void OcrService::buildBackends()
{
    // Preference order (DESIGN.md §6): WeChat (best Chinese, when installed) ->
    // Tesseract (reliable fallback). A Settings [ocr] backend pin moves the
    // named backend to the front but never removes the fallbacks.
    const QString preferred = Settings::instance().value(QStringLiteral("ocr/backend")).toString();

    // WeChat only when a local install is present (never bundled, AGENTS.md).
    if (WeChatOcr::isAvailable()) {
        auto *wechat = new WeChatOcr(this);
        m_backends << wechat;
        qCInfo(w2tCore) << "OCR backend available: wechat (" << WeChatOcr::probeInstallPath() << ")";
    } else {
        qCInfo(w2tCore) << "OCR backend wechat: not installed (backend hidden)";
    }

#ifdef WAY2TRANS_HAVE_TESSERACT
    {
        auto *tess = new TesseractOcr(this);
        m_backends << tess;
        qCInfo(w2tCore) << "OCR backend available: tesseract, languages" << TesseractOcr::availableLanguages();
    }
#else
    qCInfo(w2tCore) << "OCR backend tesseract: not compiled (WAY2TRANS_HAVE_TESSERACT off)";
#endif

    // Apply the Settings pin: stable-move the preferred backend to the front.
    if (!preferred.isEmpty()) {
        for (int i = 0; i < m_backends.size(); ++i) {
            if (m_backends.at(i)->id() == preferred) {
                m_backends.move(i, 0);
                qCInfo(w2tCore) << "OCR preferred backend pinned to front:" << preferred;
                break;
            }
        }
    }

    // Wire every backend's signals through a per-request sequence guard so a
    // late signal from a superseded request is ignored.
    for (OcrBackend *backend : std::as_const(m_backends)) {
        const quint64 seqAtConnect = 0;
        Q_UNUSED(seqAtConnect);
        connect(backend, &OcrBackend::recognized, this, [this, backend](const QString &text) {
            if (m_currentIndex < 0 || m_backends.value(m_currentIndex) != backend)
                return;
            const bool mergeWraps
                = Settings::instance().value(QStringLiteral("general/mergeHardWraps"), true).toBool();
            const QString cleaned = TextCleaner::clean(text, mergeWraps);
            qCInfo(w2tCore) << "OCR recognized via" << backend->id() << "len" << cleaned.size();
            m_currentIndex = -1;
            emit recognized(cleaned);
        });
        connect(backend, &OcrBackend::failed, this, [this, backend](const QString &reason) {
            if (m_currentIndex < 0 || m_backends.value(m_currentIndex) != backend)
                return;
            qCWarning(w2tCore) << "OCR backend" << backend->id() << "failed:" << reason << "- trying next";
            tryBackend(m_currentIndex + 1);
        });
    }
}

QStringList OcrService::availableBackendIds() const
{
    QStringList ids;
    for (const OcrBackend *b : m_backends)
        ids << b->id();
    return ids;
}

bool OcrService::hasBackend() const
{
    return !m_backends.isEmpty();
}

QString OcrService::unavailableHint() const
{
    if (hasBackend())
        return {};
    return tr("No OCR backend available. Install Tesseract (libtesseract + the "
              "tesseract-ocr-eng / tesseract-ocr-chi-sim language packs), or install WeChat "
              "for higher-quality Chinese OCR.");
}

void OcrService::recognize(const QImage &image, const QStringList &langHints)
{
    if (!hasBackend()) {
        emit failed(unavailableHint());
        return;
    }
    if (image.isNull()) {
        emit failed(tr("No image to recognise"));
        return;
    }
    m_pendingImage = image;
    m_pendingHints = langHints;
    ++m_requestSeq;
    tryBackend(0);
}

void OcrService::tryBackend(int index)
{
    if (index < 0 || index >= m_backends.size()) {
        m_currentIndex = -1;
        emit failed(tr("All OCR backends failed to recognise the image"));
        return;
    }
    m_currentIndex = index;
    OcrBackend *backend = m_backends.at(index);
    qCInfo(w2tCore) << "OCR: trying backend" << backend->id() << "(" << index + 1 << "of" << m_backends.size() << ")";
    backend->recognize(m_pendingImage, m_pendingHints);
}
