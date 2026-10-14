#pragma once

#include "engines/TranslateTypes.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class TranslateEngine;
class QNetworkAccessManager;

// Orchestrates translation across the enabled engines (DESIGN.md §5).
//
// A single translate() call fans the request out to every enabled engine
// concurrently and re-emits each engine's result as it lands, so the UI can pop
// a skeleton immediately and fill cards in as they arrive. Results for identical
// request text are cached in memory for a short window to avoid re-hitting the
// web endpoints (DESIGN.md §5.1 rate-limiting). The service owns the shared
// QNetworkAccessManager so all engine traffic flows through one manager
// (AGENTS.md network rule); platform/engine layers never build their own.
class TranslateService : public QObject {
    Q_OBJECT

public:
    explicit TranslateService(QObject *parent = nullptr);

    // (Re)build the engine set from the service instance list ([services/<id>],
    // see ServiceInstances): one engine object per enabled instance.
    void reloadEngines();

    // The engine (service instance) ids currently active, in display order.
    QStringList activeEngineIds() const;

    // User-visible name for an active engine id (falls back to the id).
    QString engineDisplayName(const QString &engineId) const;

    // Detect the source language, apply the target-language rule and fan the
    // request out to all enabled engines. Emits requestStarted() with the
    // resolved request, then resultReady() per engine (cache hits included).
    void translate(const QString &rawText);

    // Re-run a single engine for the last request (retry button on a failed
    // card). No-op if there is no last request or the id is unknown.
    void retry(const QString &engineId);

    // Resolve the target language for a detected source, per configured rule.
    // One-period rule: Chinese source -> English, otherwise -> Chinese.
    static QString targetLangFor(const QString &sourceLang);

    // The shared network manager (AGENTS.md: one manager for all engine traffic).
    // Exposed so a one-shot connectivity-test engine can reuse it.
    QNetworkAccessManager *networkManager() const { return m_nam; }

signals:
    // Emitted once per translate() call with the fully-resolved request (after
    // cleaning + language detection) so the UI can render the header/skeleton.
    void requestStarted(const TranslateRequest &request, const QStringList &engineIds);
    void resultReady(const TranslateResult &result);
    // Streaming delta from an engine (LLM). engineId identifies the source card;
    // the accumulated text is finalised by the matching resultReady.
    void partialResult(const QString &engineId, const QString &delta);

private:
    void dispatchTo(TranslateEngine *engine, const TranslateRequest &req);

    struct CacheEntry {
        TranslateResult result;
        QDateTime at;
    };

    QNetworkAccessManager *m_nam;
    QList<TranslateEngine *> m_engines; // owned via QObject parent (this)
    QHash<QString, CacheEntry> m_cache; // key: engineId + '\n' + requestText
    TranslateRequest m_lastRequest;
};
