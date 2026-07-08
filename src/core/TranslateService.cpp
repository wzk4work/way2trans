#include "TranslateService.h"

#include "LangDetector.h"
#include "Logging.h"
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

    Settings &settings = Settings::instance();
    for (const EngineRegistry::Entry &entry : EngineRegistry::entries()) {
        const QString key = QStringLiteral("engines/%1/enabled").arg(entry.id);
        const bool enabled = settings.value(key, entry.defaultEnabled).toBool();
        if (!enabled)
            continue;
        TranslateEngine *engine = EngineRegistry::create(entry.id, m_nam, this);
        if (!engine)
            continue;
        connect(engine, &TranslateEngine::resultReady, this, [this](const TranslateResult &result) {
            if (result.ok())
                m_cache.insert(
                    cacheKey(result.engineId, m_lastRequest.text), CacheEntry {result, QDateTime::currentDateTime()});
            emit resultReady(result);
        });
        m_engines << engine;
    }
    qCInfo(w2tCore) << "TranslateService active engines:" << activeEngineIds();
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
    // One-period hard-coded rule (reserved as a Settings key for later): a
    // Chinese source translates to English, everything else to Chinese.
    const QString rule
        = Settings::instance().value(QStringLiteral("general/targetLangRule"), QStringLiteral("zh-en")).toString();
    Q_UNUSED(rule); // only one rule implemented in M1
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
