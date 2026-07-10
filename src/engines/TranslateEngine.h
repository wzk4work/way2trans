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
//
// One engine class can back several *service instances* (Bob/Pot model): the
// service layer binds an instance via setInstance(), which gives the engine its
// public id, its user-visible name and the Settings group its configuration
// lives in. Unbound engines fall back to the legacy per-type [engines/<type>]
// group, which keeps single-instance uses (and tests) working unchanged.
class TranslateEngine : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~TranslateEngine() override = default;

    virtual QString typeId() const = 0; // engine type: "google" / "bing" / ...
    virtual QString typeName() const = 0; // default user-facing engine name

    // Bind this engine to a service instance (id used in results/retry
    // routing, the instance's display name and its Settings config group).
    void setInstance(const QString &instanceId, const QString &name, const QString &configGroup)
    {
        m_instanceId = instanceId;
        m_instanceName = name;
        m_configGroup = configGroup;
    }

    // Public identity: the bound instance when set, the type otherwise.
    QString id() const { return m_instanceId.isEmpty() ? typeId() : m_instanceId; }
    QString displayName() const { return m_instanceName.isEmpty() ? typeName() : m_instanceName; }

    // Kick off a translation. Result arrives via resultReady().
    virtual void translate(const TranslateRequest &req) = 0;

    // Best-effort cancellation of any in-flight request.
    virtual void cancel() = 0;

signals:
    void resultReady(const TranslateResult &result);
    void partialResult(const QString &delta); // streaming engines (LLM, later)

protected:
    // The Settings group this engine reads its configuration from.
    QString configGroup() const
    {
        return m_configGroup.isEmpty() ? QStringLiteral("engines/%1").arg(typeId()) : m_configGroup;
    }

private:
    QString m_instanceId;
    QString m_instanceName;
    QString m_configGroup;
};
