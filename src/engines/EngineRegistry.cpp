#include "EngineRegistry.h"

#include "BingWebEngine.h"
#include "DeepLEngine.h"
#include "GoogleWebEngine.h"
#include "LlmEngine.h"
#include "TranslateEngine.h"

const std::vector<EngineRegistry::Entry> &EngineRegistry::entries()
{
    // Built-in engines, in display order. Both free web engines ship enabled so
    // the app translates with zero configuration (DESIGN.md §5.1).
    static const std::vector<Entry> kEntries = {
        {QStringLiteral("google"), QStringLiteral("Google"), true,
            [](QNetworkAccessManager *nam, QObject *parent) -> TranslateEngine * {
                return new GoogleWebEngine(nam, parent);
            }},
        {QStringLiteral("bing"), QStringLiteral("Bing"), true,
            [](QNetworkAccessManager *nam, QObject *parent) -> TranslateEngine * {
                return new BingWebEngine(nam, parent);
            }},
        // Key-gated engines ship disabled: they need an API key before they can
        // do anything, so enabling them by default would only produce error
        // cards (DESIGN.md §5.1).
        {QStringLiteral("deepl"), QStringLiteral("DeepL"), false,
            [](QNetworkAccessManager *nam, QObject *parent) -> TranslateEngine * {
                return new DeepLEngine(nam, parent);
            }},
        {QStringLiteral("llm"), QStringLiteral("LLM"), false,
            [](QNetworkAccessManager *nam, QObject *parent) -> TranslateEngine * {
                return new LlmEngine(nam, parent);
            }},
    };
    return kEntries;
}

QStringList EngineRegistry::defaultEnabledIds()
{
    QStringList ids;
    for (const Entry &e : entries()) {
        if (e.defaultEnabled)
            ids << e.id;
    }
    return ids;
}

TranslateEngine *EngineRegistry::create(const QString &id, QNetworkAccessManager *nam, QObject *parent)
{
    for (const Entry &e : entries()) {
        if (e.id == id)
            return e.factory(nam, parent);
    }
    return nullptr;
}
