#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class TranslateEngine;
class QNetworkAccessManager;
class QObject;

// Static registry of the built-in translation engines (DESIGN.md §5.1).
//
// One-period engines register themselves here; the second period may switch to
// QPluginLoader for third-party plugins. Engines are created lazily with a
// caller-provided QNetworkAccessManager so that all HTTP traffic flows through
// the single network manager owned by core/ (AGENTS.md network rule).
class EngineRegistry {
public:
    // Factory that builds one engine instance, wiring in the shared network
    // manager and a QObject parent for lifetime management.
    using Factory = std::function<TranslateEngine *(QNetworkAccessManager *nam, QObject *parent)>;

    struct Entry {
        QString id;
        QString displayName;
        bool defaultEnabled = false;
        Factory factory;
    };

    // The list of every registered engine, in registration order.
    static const std::vector<Entry> &entries();

    // Ids of engines that ship enabled out of the box (DESIGN.md §5.1).
    static QStringList defaultEnabledIds();

    // Create the engine with the given id, or nullptr if unknown.
    static TranslateEngine *create(const QString &id, QNetworkAccessManager *nam, QObject *parent);
};
