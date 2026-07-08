#include "HistoryStore.h"

#include "Logging.h"
#include "Storage.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
// Build a HistoryRecord from the current row of a SELECT that pulls the columns
// in the canonical order below.
HistoryRecord recordFromQuery(const QSqlQuery &q)
{
    HistoryRecord r;
    r.id = q.value(0).toLongLong();
    r.createdAt = q.value(1).toLongLong();
    r.trigger = q.value(2).toString();
    r.sourceText = q.value(3).toString();
    r.sourceLang = q.value(4).toString();
    r.targetLang = q.value(5).toString();
    r.engineId = q.value(6).toString();
    r.resultText = q.value(7).toString();
    r.favorite = q.value(8).toInt() != 0;
    return r;
}

constexpr auto kSelectCols
    = "id, created_at, trigger, source_text, source_lang, target_lang, engine_id, result_text, favorite";

// Escape a user query for an FTS5 MATCH: wrap each whitespace token in double
// quotes so punctuation/operators in selection text can't break the parse.
QString toFtsMatch(const QString &query)
{
    const QStringList tokens = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList quoted;
    for (QString t : tokens) {
        t.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        quoted << QStringLiteral("\"%1\"").arg(t);
    }
    return quoted.join(QLatin1Char(' '));
}
} // namespace

HistoryStore::HistoryStore(Storage *storage)
    : m_storage(storage)
{ }

qint64 HistoryStore::record(const HistoryRecord &record, int dedupeWindowSecs)
{
    QSqlDatabase db = m_storage->database();
    const qint64 now = record.createdAt > 0 ? record.createdAt : QDateTime::currentSecsSinceEpoch();

    // De-dupe: refresh an identical recent row rather than appending (DESIGN §8.2).
    {
        QSqlQuery find(db);
        find.prepare(QStringLiteral("SELECT id FROM history WHERE source_text = ? AND created_at >= ? "
                                    "ORDER BY created_at DESC LIMIT 1"));
        find.addBindValue(record.sourceText);
        find.addBindValue(now - dedupeWindowSecs);
        if (find.exec() && find.next()) {
            const qint64 existingId = find.value(0).toLongLong();
            QSqlQuery upd(db);
            upd.prepare(QStringLiteral("UPDATE history SET created_at = ?, trigger = ?, source_lang = ?, "
                                       "target_lang = ?, engine_id = ?, result_text = ? WHERE id = ?"));
            upd.addBindValue(now);
            upd.addBindValue(record.trigger);
            upd.addBindValue(record.sourceLang);
            upd.addBindValue(record.targetLang);
            upd.addBindValue(record.engineId);
            upd.addBindValue(record.resultText);
            upd.addBindValue(existingId);
            if (!upd.exec()) {
                qCWarning(w2tCore) << "history dedupe update failed:" << upd.lastError().text();
                return -1;
            }
            return existingId;
        }
    }

    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO history"
                               " (created_at, trigger, source_text, source_lang, target_lang, engine_id, result_text,"
                               "  favorite) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    ins.addBindValue(now);
    ins.addBindValue(record.trigger);
    ins.addBindValue(record.sourceText);
    ins.addBindValue(record.sourceLang);
    ins.addBindValue(record.targetLang);
    ins.addBindValue(record.engineId);
    ins.addBindValue(record.resultText);
    ins.addBindValue(record.favorite ? 1 : 0);
    if (!ins.exec()) {
        qCWarning(w2tCore) << "history insert failed:" << ins.lastError().text();
        return -1;
    }
    return ins.lastInsertId().toLongLong();
}

QList<HistoryRecord> HistoryStore::recent(int limit) const
{
    QList<HistoryRecord> out;
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("SELECT %1 FROM history ORDER BY created_at DESC LIMIT ?").arg(kSelectCols));
    q.addBindValue(limit);
    if (!q.exec()) {
        qCWarning(w2tCore) << "history recent failed:" << q.lastError().text();
        return out;
    }
    while (q.next())
        out << recordFromQuery(q);
    return out;
}

QList<HistoryRecord> HistoryStore::search(const QString &query, int limit) const
{
    const QString match = toFtsMatch(query);
    if (match.isEmpty())
        return recent(limit);

    QList<HistoryRecord> out;
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("SELECT %1 FROM history WHERE id IN "
                             "(SELECT rowid FROM history_fts WHERE history_fts MATCH ?) "
                             "ORDER BY created_at DESC LIMIT ?")
                  .arg(kSelectCols));
    q.addBindValue(match);
    q.addBindValue(limit);
    if (!q.exec()) {
        qCWarning(w2tCore) << "history search failed:" << q.lastError().text();
        return out;
    }
    while (q.next())
        out << recordFromQuery(q);
    return out;
}

QList<HistoryRecord> HistoryStore::byDateRange(qint64 startEpoch, qint64 endEpoch) const
{
    QList<HistoryRecord> out;
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("SELECT %1 FROM history WHERE created_at >= ? AND created_at < ? "
                             "ORDER BY created_at DESC")
                  .arg(kSelectCols));
    q.addBindValue(startEpoch);
    q.addBindValue(endEpoch);
    if (!q.exec()) {
        qCWarning(w2tCore) << "history byDateRange failed:" << q.lastError().text();
        return out;
    }
    while (q.next())
        out << recordFromQuery(q);
    return out;
}

bool HistoryStore::setFavorite(qint64 id, bool favorite)
{
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("UPDATE history SET favorite = ? WHERE id = ?"));
    q.addBindValue(favorite ? 1 : 0);
    q.addBindValue(id);
    if (!q.exec()) {
        qCWarning(w2tCore) << "history setFavorite failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool HistoryStore::remove(qint64 id)
{
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("DELETE FROM history WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        qCWarning(w2tCore) << "history remove failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool HistoryStore::clearAll()
{
    QSqlQuery q(m_storage->database());
    if (!q.exec(QStringLiteral("DELETE FROM history"))) {
        qCWarning(w2tCore) << "history clearAll failed:" << q.lastError().text();
        return false;
    }
    return true;
}

int HistoryStore::count() const
{
    QSqlQuery q(m_storage->database());
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM history")) || !q.next())
        return 0;
    return q.value(0).toInt();
}

int HistoryStore::enforceRetention(int maxRows)
{
    if (maxRows <= 0)
        return 0; // unlimited / disabled trimming

    QSqlDatabase db = m_storage->database();
    // Count only non-favorite rows: favorites are exempt from the cap.
    QSqlQuery countQ(db);
    if (!countQ.exec(QStringLiteral("SELECT COUNT(*) FROM history WHERE favorite = 0")) || !countQ.next())
        return 0;
    const int nonFav = countQ.value(0).toInt();
    if (nonFav <= maxRows)
        return 0;

    const int toDelete = nonFav - maxRows;
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM history WHERE id IN "
                               "(SELECT id FROM history WHERE favorite = 0 ORDER BY created_at ASC LIMIT ?)"));
    del.addBindValue(toDelete);
    if (!del.exec()) {
        qCWarning(w2tCore) << "history retention delete failed:" << del.lastError().text();
        return 0;
    }
    return del.numRowsAffected();
}
