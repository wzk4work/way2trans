#include "ServiceInstances.h"

#include "Settings.h"

#include <QHash>
#include <QUuid>

#include <algorithm>

namespace {
constexpr auto kSeededKey = "services/seeded";

QString key(const QString &id, const char *field)
{
    return QStringLiteral("services/%1/%2").arg(id, QLatin1String(field));
}

QString newInstanceId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Copy a legacy [engines/<type>] key into an instance group when present.
void migrateKey(const QString &legacyGroup, const QString &instanceId, const char *field)
{
    Settings &s = Settings::instance();
    const QVariant v = s.value(legacyGroup + QLatin1Char('/') + QLatin1String(field));
    if (v.isValid() && !v.toString().isEmpty())
        s.setValue(key(instanceId, field), v);
}
} // namespace

const QList<ServiceTemplate> &ServiceInstances::templates()
{
    static const QList<ServiceTemplate> kTemplates = {
        {QStringLiteral("google"), QStringLiteral("google"), QStringLiteral("Google"), false, {}},
        {QStringLiteral("bing"), QStringLiteral("bing"), QStringLiteral("Bing"), false, {}},
        {QStringLiteral("deepl"), QStringLiteral("deepl"), QStringLiteral("DeepL"), true, {}},
        {QStringLiteral("llm-openai"), QStringLiteral("llm"), QStringLiteral("OpenAI"), true,
            {{QStringLiteral("endpoint"), QStringLiteral("https://api.openai.com/v1")},
                {QStringLiteral("model"), QStringLiteral("gpt-4o-mini")}}},
        {QStringLiteral("llm-deepseek"), QStringLiteral("llm"), QStringLiteral("DeepSeek"), true,
            {{QStringLiteral("endpoint"), QStringLiteral("https://api.deepseek.com/v1")},
                {QStringLiteral("model"), QStringLiteral("deepseek-chat")}}},
        {QStringLiteral("llm-ollama"), QStringLiteral("llm"), QStringLiteral("Ollama"), false,
            {{QStringLiteral("endpoint"), QStringLiteral("http://localhost:11434/v1")},
                {QStringLiteral("model"), QStringLiteral("llama3.1")}}},
        {QStringLiteral("llm-custom"), QStringLiteral("llm"), QStringLiteral("OpenAI-compatible"), true,
            {{QStringLiteral("endpoint"), QString()}, {QStringLiteral("model"), QString()}}},
    };
    return kTemplates;
}

const ServiceTemplate *ServiceInstances::templateById(const QString &templateId)
{
    for (const ServiceTemplate &tpl : templates()) {
        if (tpl.id == templateId)
            return &tpl;
    }
    return nullptr;
}

QList<ServiceInstance> ServiceInstances::all()
{
    Settings &s = Settings::instance();
    QList<ServiceInstance> instances;
    const QStringList ids = s.childGroups(QStringLiteral("services"));
    for (const QString &id : ids) {
        ServiceInstance inst;
        inst.id = id;
        inst.engineType = s.value(key(id, "type")).toString();
        inst.name = s.value(key(id, "name"), inst.engineType).toString();
        inst.enabled = s.value(key(id, "enabled"), false).toBool();
        inst.keyGated = s.value(key(id, "keyGated"), false).toBool();
        if (!inst.engineType.isEmpty())
            instances.append(inst);
    }
    std::stable_sort(instances.begin(), instances.end(), [&s](const ServiceInstance &a, const ServiceInstance &b) {
        return s.value(key(a.id, "order"), 0).toInt() < s.value(key(b.id, "order"), 0).toInt();
    });
    return instances;
}

QString ServiceInstances::add(const ServiceTemplate &tpl)
{
    Settings &s = Settings::instance();
    const QString id = newInstanceId();
    s.setValue(key(id, "type"), tpl.engineType);
    s.setValue(key(id, "name"), tpl.name);
    // Key-gated services start disabled: they can't work until configured.
    s.setValue(key(id, "enabled"), !tpl.keyGated);
    s.setValue(key(id, "keyGated"), tpl.keyGated);
    s.setValue(key(id, "order"), int(all().size())); // append last
    for (const auto &def : tpl.defaults) {
        if (!def.second.isEmpty())
            s.setValue(QStringLiteral("services/%1/%2").arg(id, def.first), def.second);
    }
    return id;
}

void ServiceInstances::remove(const QString &id)
{
    Settings::instance().remove(QStringLiteral("services/%1").arg(id));
}

void ServiceInstances::setEnabled(const QString &id, bool enabled)
{
    Settings::instance().setValue(key(id, "enabled"), enabled);
}

void ServiceInstances::rename(const QString &id, const QString &name)
{
    Settings::instance().setValue(key(id, "name"), name);
}

void ServiceInstances::setOrder(const QStringList &orderedIds)
{
    for (int i = 0; i < orderedIds.size(); ++i)
        Settings::instance().setValue(key(orderedIds.at(i), "order"), i);
}

QString ServiceInstances::configGroup(const QString &id)
{
    return QStringLiteral("services/%1").arg(id);
}

void ServiceInstances::ensureSeeded()
{
    Settings &s = Settings::instance();
    if (s.value(QLatin1String(kSeededKey), false).toBool())
        return;

    // Legacy layout (pre-instances): one implicit service per engine type under
    // [engines/<type>]. Carry over enabled/order/config so an upgrade keeps the
    // user's engines exactly as they were.
    struct LegacySeed {
        const char *templateId;
        const char *legacyGroup;
        bool defaultEnabled;
        QList<const char *> fields;
    };
    QList<LegacySeed> seeds = {
        {"google", "engines/google", true, {"host"}},
        {"bing", "engines/bing", true, {}},
        {"deepl", "engines/deepl", false, {"apiKey"}},
        {"llm-custom", "engines/llm", false, {"apiKey", "endpoint", "model", "prompt"}},
    };
    // Respect a legacy display order when the user had configured one
    // (default: declaration order), then create instances in that sequence —
    // add() appends, so the persisted order falls out naturally.
    QHash<QString, int> declIndex;
    for (int i = 0; i < seeds.size(); ++i)
        declIndex.insert(QLatin1String(seeds.at(i).legacyGroup), i);
    std::stable_sort(seeds.begin(), seeds.end(), [&s, &declIndex](const LegacySeed &a, const LegacySeed &b) {
        const QString ga = QLatin1String(a.legacyGroup);
        const QString gb = QLatin1String(b.legacyGroup);
        return s.value(ga + QStringLiteral("/order"), declIndex.value(ga)).toInt()
            < s.value(gb + QStringLiteral("/order"), declIndex.value(gb)).toInt();
    });
    for (const LegacySeed &seed : std::as_const(seeds)) {
        const ServiceTemplate *tpl = templateById(QLatin1String(seed.templateId));
        if (!tpl)
            continue;
        const QString id = add(*tpl);
        const QString legacy = QLatin1String(seed.legacyGroup);
        s.setValue(key(id, "enabled"), s.value(legacy + QStringLiteral("/enabled"), seed.defaultEnabled).toBool());
        for (const char *field : seed.fields)
            migrateKey(legacy, id, field);
    }
    s.setValue(QLatin1String(kSeededKey), true);
}
