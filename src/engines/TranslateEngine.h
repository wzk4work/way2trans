#pragma once

#include "TranslateTypes.h"

#include <QObject>
#include <QString>
#include <QStringList>

// Abstract translation engine (DESIGN.md §5).
//
// Every engine is a QObject with an asynchronous, signal-based contract: a call
// to translate() must never block and must never throw; failures are reported by
// emitting a TranslateResult with a non-empty error field. Concrete engines keep
// all of their request/response marshalling inside a single implementation file
// so that reverse-engineered web endpoints can be patched with minimal blast
// radius (DESIGN.md §5.1).
class TranslateEngine : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~TranslateEngine() override = default;

    virtual QString id() const = 0; // "google" / "bing" / ...
    virtual QString displayName() const = 0; // user-facing engine name
    virtual QStringList configKeys() const = 0; // config keys this engine reads

    // Kick off a translation. Result arrives via resultReady().
    virtual void translate(const TranslateRequest &req) = 0;

    // Best-effort cancellation of any in-flight request.
    virtual void cancel() = 0;

signals:
    void resultReady(const TranslateResult &result);
    void partialResult(const QString &delta); // streaming engines (LLM, later)
};
