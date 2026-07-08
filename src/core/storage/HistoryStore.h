#pragma once

#include <QList>
#include <QString>

class Storage;

// A single history row (DESIGN.md §8.1 history table).
struct HistoryRecord {
    qint64 id = 0;
    qint64 createdAt = 0; // unix epoch seconds
    QString trigger; // selection | capture | input | clipboard
    QString sourceText;
    QString sourceLang;
    QString targetLang;
    QString engineId; // engine behind the displayed primary result
    QString resultText;
    bool favorite = false;
};

// CRUD + search over the history table (DESIGN.md §8.2).
//
// Writes one row per translation (the first successful result); a repeat of the
// same source_text within a short window updates the timestamp in place instead
// of piling up duplicates. Full-text search runs through the FTS5 mirror. A
// retention cap trims the oldest non-favorite rows.
class HistoryStore {
public:
    explicit HistoryStore(Storage *storage);

    // Record a translation. If an identical source_text exists and was created
    // within dedupeWindowSecs, its timestamp/result are refreshed and its id is
    // returned; otherwise a new row is inserted. Returns the row id, or -1 on
    // failure. Applies the retention policy afterwards.
    qint64 record(const HistoryRecord &record, int dedupeWindowSecs = 600);

    // Most-recent-first rows, newest first, up to limit.
    QList<HistoryRecord> recent(int limit = 200) const;

    // FTS5 search over source_text + result_text, newest match first.
    QList<HistoryRecord> search(const QString &query, int limit = 200) const;

    // Rows created within [startEpoch, endEpoch), newest first.
    QList<HistoryRecord> byDateRange(qint64 startEpoch, qint64 endEpoch) const;

    bool setFavorite(qint64 id, bool favorite);
    bool remove(qint64 id);
    bool clearAll();

    int count() const;

    // Trim to at most maxRows non-favorite rows, dropping the oldest. maxRows <= 0
    // means "unlimited" (no trimming). Favorites are never trimmed (DESIGN.md §8.2).
    // Returns the number of rows deleted.
    int enforceRetention(int maxRows);

private:
    Storage *m_storage;
};
