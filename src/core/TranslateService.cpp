#include "TranslateService.h"

#include "LangDetector.h"
#include "Logging.h"
#include "ServiceInstances.h"
#include "Settings.h"
#include "TextCleaner.h"
#include "engines/EngineRegistry.h"
#include "engines/TranslateEngine.h"

#include <QNetworkAccessManager>

namespace {
// Results for the same (engine, text) pair are reused within this window.
constexpr int kCacheTtlSeconds = 30;

QString cacheKey(const QString &engineId, const QString &text)
{
    return engineId + QLatin1Char('\n') + text;
}
} // namespace

TranslateService::TranslateService(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    reloadEngines();
}

void TranslateService::reloadEngines()
{
    qDeleteAll(m_engines);
    m_engines.clear();

    // One engine object per enabled service instance ([services/<uuid>],
    // Bob/Pot model): the instance list is already in display order, which
    // decides the order of result cards in the popup and main window.
    ServiceInstances::ensureSeeded();
    const QList<ServiceInstance> instances = ServiceInstances::all();
    for (const ServiceInstance &inst : instances) {
        if (!inst.enabled)
            continue;
        TranslateEngine *engine = EngineRegistry::create(inst.engineType, m_nam, this);
        if (!engine)
            continue;
        engine->setInstance(inst.id, inst.name, ServiceInstances::configGroup(inst.id));
        connect(engine, &TranslateEngine::resultReady, this, [this](const TranslateResult &result) {
            if (result.ok())
                m_cache.insert(
                    cacheKey(result.engineId, m_lastRequest.text), CacheEntry {result, QDateTime::currentDateTime()});
            emit resultReady(result);
        });
        // Forward streaming deltas, tagging them with the instance id so the UI
        // can route them to the right card (LLM streaming, DESIGN.md §5).
        const QString engineId = engine->id();
        connect(engine, &TranslateEngine::partialResult, this,
            [this, engineId](const QString &delta) { emit partialResult(engineId, delta); });
        m_engines << engine;
    }
    qCInfo(w2tCore) << "TranslateService active engines:" << activeEngineIds();
}

QString TranslateService::engineDisplayName(const QString &engineId) const
{
    for (const TranslateEngine *e : m_engines) {
        if (e->id() == engineId)
            return e->displayName();
    }
    return engineId;
}

QStringList TranslateService::activeEngineIds() const
{
    QStringList ids;
    for (const TranslateEngine *e : m_engines)
        ids << e->id();
    return ids;
}

QString TranslateService::targetLangFor(const QString &sourceLang)
{
    // Settings -> General "language pair rule". The fixed-target rules exist
    // mainly as an escape hatch for when script detection can't be trusted
    // (OCR noise): they pin the target no matter what the source looks like.
    const QString rule
        = Settings::instance().value(QStringLiteral("general/targetLangRule"), QStringLiteral("zh-en")).toString();
    if (rule == QLatin1String("to-zh"))
        return QStringLiteral("zh-CN");
    if (rule == QLatin1String("to-en"))
        return QStringLiteral("en");
    // Default "zh-en": a Chinese source translates to English, everything
    // else to Chinese.
    if (sourceLang.startsWith(QStringLiteral("zh")))
        return QStringLiteral("en");
    return QStringLiteral("zh-CN");
}

void TranslateService::translate(const QString &rawText)
{
    const bool mergeWraps = Settings::instance().value(QStringLiteral("general/mergeHardWraps"), true).toBool();
    const QString text = TextCleaner::clean(rawText, mergeWraps);
    if (text.isEmpty()) {
        qCDebug(w2tCore) << "translate() called with empty text after cleaning";
        return;
    }

    const QString sourceLang = LangDetector::detectLang(text);
    TranslateRequest req;
    req.text = text;
    req.sourceLang = sourceLang == QLatin1String("auto") ? QStringLiteral("auto") : sourceLang;
    req.targetLang = targetLangFor(sourceLang);
    m_lastRequest = req;

    qCInfo(w2tCore) << "translate len" << text.size() << "src" << req.sourceLang << "tgt" << req.targetLang << "engines"
                    << activeEngineIds();

    emit requestStarted(req, activeEngineIds());

    for (TranslateEngine *engine : m_engines)
        dispatchTo(engine, req);
}

void TranslateService::retry(const QString &engineId)
{
    if (m_lastRequest.text.isEmpty())
        return;
    for (TranslateEngine *engine : m_engines) {
        if (engine->id() == engineId) {
            m_cache.remove(cacheKey(engineId, m_lastRequest.text));
            dispatchTo(engine, m_lastRequest);
            return;
        }
    }
}

void TranslateService::dispatchTo(TranslateEngine *engine, const TranslateRequest &req)
{
    // Serve from the short-lived cache when fresh (DESIGN.md §5.1).
    const auto it = m_cache.constFind(cacheKey(engine->id(), req.text));
    if (it != m_cache.constEnd() && it->at.secsTo(QDateTime::currentDateTime()) < kCacheTtlSeconds) {
        qCDebug(w2tCore) << "cache hit for" << engine->id();
        // Re-emit asynchronously so listeners see consistent ordering with live
        // results (always after requestStarted).
        const TranslateResult cached = it->result;
        QMetaObject::invokeMethod(this, [this, cached]() { emit resultReady(cached); }, Qt::QueuedConnection);
        return;
    }
    engine->translate(req);
}
