#pragma once

#include "engines/TranslateTypes.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class Storage;

// A single vocabulary entry (DESIGN.md §8.1 vocab table).
struct VocabRecord {
    qint64 id = 0;
    QString word;
    QString lang;
    QString phonetic;
    std::optional<DictEntry> dict; // snapshot of the engine's dictionary data
    QString translation; // fallback when there is no DictEntry
    QString context; // sentence the word was collected in, may be empty
    QString note; // user note
    QStringList tags;
    int status = 0; // 0 new / 1 familiar / 2 mastered
    qint64 addedAt = 0;
    qint64 lastSeenAt = 0;
    int lookupCount = 1;
};

// CRUD, ranking and export over the vocab table (DESIGN.md §8.3).
//
// add() upserts on UNIQUE(word,lang): a repeated lookup bumps lookup_count and
// refreshes last_seen_at instead of duplicating (the "repeat lookup is a signal"
// rule). Entries can be listed by lookup_count or added_at, filtered by status,
// and exported to Anki TSV / CSV / JSON. JSON is a full backup that round-trips
// back in via importJson(), merging on conflict by keeping the higher
// lookup_count.
class VocabStore {
public:
    enum class Sort { LookupCountDesc, AddedAtDesc };

    explicit VocabStore(Storage *storage);

    // Insert the entry, or on a UNIQUE(word,lang) conflict bump lookup_count and
    // refresh last_seen_at (and fill any snapshot fields that were empty).
    // Returns the row id, or -1 on failure.
    qint64 add(const VocabRecord &record);

    // Remove the entry for (word, lang). Returns true if a row was deleted.
    bool remove(const QString &word, const QString &lang);
    bool removeById(qint64 id);

    // Whether (word, lang) is already saved (drives the popup star state).
    bool contains(const QString &word, const QString &lang) const;

    // Fetch one entry, if present.
    std::optional<VocabRecord> find(const QString &word, const QString &lang) const;

    // All entries in the requested order, optionally filtered to one status
    // (statusFilter < 0 means all statuses).
    QList<VocabRecord> list(Sort sort = Sort::LookupCountDesc, int statusFilter = -1) const;

    bool setStatus(qint64 id, int status);
    bool setNote(qint64 id, const QString &note);
    bool setTags(qint64 id, const QStringList &tags);
    bool setContext(qint64 id, const QString &context);

    int count() const;

    // --- Export (DESIGN.md §8.3) -----------------------------------------
    // Anki import file: tab-separated word|phonetic|definitions|context|note.
    QString exportAnkiTsv() const;
    // Generic spreadsheet CSV with a header row and RFC-4180 quoting.
    QString exportCsv() const;
    // Full JSON backup (every field), re-importable via importJson().
    QString exportJson() const;

    // Merge a JSON backup produced by exportJson(). On UNIQUE(word,lang) conflict
    // the row with the higher lookup_count wins (DESIGN.md §8.3). Returns the
    // number of entries added or updated, or -1 on a parse error.
    int importJson(const QString &json);

    // Flatten a DictEntry's meanings into a single human-readable string (used by
    // the TSV/CSV "definitions" column). Static + pure for tests.
    static QString flattenDefinitions(const DictEntry &dict);

private:
    Storage *m_storage;
};
