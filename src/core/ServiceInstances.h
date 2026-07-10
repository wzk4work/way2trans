#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Translation service instances (DESIGN.md §5.1).
//
// Bob/Pot-style model: a fixed catalog of *templates* (an engine type plus
// preset defaults — e.g. the one LLM engine ships as OpenAI, DeepSeek, Ollama
// and custom presets) and a user-managed list of *instances*. Each instance is
// an independent, reorderable service with its own name and configuration, so
// two differently-configured LLM services can run side by side.
//
// Instances persist under [services/<uuid>] in Settings:
//   type     engine type id (EngineRegistry: google/bing/deepl/llm)
//   name     user-visible instance name
//   enabled  whether the service participates in translations
//   order    display/dispatch position
//   keyGated whether the UI should warn when apiKey is empty
//   …plus the engine's own config keys (apiKey/endpoint/model/prompt/host).

struct ServiceTemplate {
    QString id; // catalog id, e.g. "llm-deepseek"
    QString engineType; // EngineRegistry engine id
    QString name; // default instance name (proper nouns, not translated)
    bool keyGated = false; // needs an API key to be useful
    QList<QPair<QString, QString>> defaults; // config key -> preset value
};

struct ServiceInstance {
    QString id; // uuid (settings group name)
    QString engineType;
    QString name;
    bool enabled = false;
    bool keyGated = false;
};

class ServiceInstances {
public:
    // The built-in template catalog, in menu order.
    static const QList<ServiceTemplate> &templates();
    static const ServiceTemplate *templateById(const QString &templateId);

    // All instances, sorted by their persisted order.
    static QList<ServiceInstance> all();

    // Create an instance from a template (appended last). Returns its id.
    static QString add(const ServiceTemplate &tpl);

    static void remove(const QString &id);
    static void setEnabled(const QString &id, bool enabled);
    static void rename(const QString &id, const QString &name);

    // Persist a full display order (index = position).
    static void setOrder(const QStringList &orderedIds);

    // Settings group holding one instance's config ("services/<id>").
    static QString configGroup(const QString &id);

    // One-time migration: seed the instance list from the legacy per-type
    // [engines/<type>] keys (pre-instance layout). Idempotent — guarded by a
    // marker key, so deleting every service does not re-seed.
    static void ensureSeeded();
};
