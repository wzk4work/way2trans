#include "VocabStore.h"

#include "Logging.h"
#include "Storage.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
constexpr auto kSelectCols = "id, word, lang, phonetic, dict_json, translation, context, note, tags, status, "
                             "added_at, last_seen_at, lookup_count";

// --- DictEntry <-> JSON ----------------------------------------------------
QJsonObject dictToJson(const DictEntry &dict)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("word"), dict.word);
    obj.insert(QStringLiteral("phonetic"), dict.phonetic);
    QJsonArray meanings;
    for (const DictMeaning &m : dict.meanings) {
        QJsonObject mo;
        mo.insert(QStringLiteral("pos"), m.partOfSpeech);
        QJsonArray defs;
        for (const QString &d : m.definitions)
            defs.append(d);
        mo.insert(QStringLiteral("defs"), defs);
        meanings.append(mo);
    }
    obj.insert(QStringLiteral("meanings"), meanings);
    return obj;
}

DictEntry dictFromJson(const QJsonObject &obj)
{
    DictEntry dict;
    dict.word = obj.value(QStringLiteral("word")).toString();
    dict.phonetic = obj.value(QStringLiteral("phonetic")).toString();
    for (const QJsonValue &v : obj.value(QStringLiteral("meanings")).toArray()) {
        const QJsonObject mo = v.toObject();
        DictMeaning m;
        m.partOfSpeech = mo.value(QStringLiteral("pos")).toString();
        for (const QJsonValue &d : mo.value(QStringLiteral("defs")).toArray())
            m.definitions << d.toString();
        dict.meanings << m;
    }
    return dict;
}

QString dictToJsonString(const std::optional<DictEntry> &dict)
{
    if (!dict.has_value() || dict->isEmpty())
        return {};
    return QString::fromUtf8(QJsonDocument(dictToJson(*dict)).toJson(QJsonDocument::Compact));
}

std::optional<DictEntry> dictFromJsonString(const QString &json)
{
    if (json.isEmpty())
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return std::nullopt;
    DictEntry d = dictFromJson(doc.object());
    if (d.isEmpty())
        return std::nullopt;
    return d;
}

VocabRecord recordFromQuery(const QSqlQuery &q)
{
    VocabRecord r;
    r.id = q.value(0).toLongLong();
    r.word = q.value(1).toString();
    r.lang = q.value(2).toString();
    r.phonetic = q.value(3).toString();
    r.dict = dictFromJsonString(q.value(4).toString());
    r.translation = q.value(5).toString();
    r.context = q.value(6).toString();
    r.note = q.value(7).toString();
    const QString tagsJson = q.value(8).toString();
    if (!tagsJson.isEmpty()) {
        for (const QJsonValue &v : QJsonDocument::fromJson(tagsJson.toUtf8()).array())
            r.tags << v.toString();
    }
    r.status = q.value(9).toInt();
    r.addedAt = q.value(10).toLongLong();
    r.lastSeenAt = q.value(11).toLongLong();
    r.lookupCount = q.value(12).toInt();
    return r;
}

QString tagsToJson(const QStringList &tags)
{
    if (tags.isEmpty())
        return {};
    QJsonArray arr;
    for (const QString &t : tags)
        arr.append(t);
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// CSV field quoting per RFC 4180: wrap in quotes and double any embedded quote
// when the field contains a comma, quote or newline.
QString csvField(const QString &value)
{
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"')) || value.contains(QLatin1Char('\n'))) {
        QString escaped = value;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return value;
}
} // namespace

VocabStore::VocabStore(Storage *storage)
    : m_storage(storage)
{ }

QString VocabStore::flattenDefinitions(const DictEntry &dict)
{
    QStringList parts;
    for (const DictMeaning &m : dict.meanings) {
        const QString defs = m.definitions.join(QStringLiteral(", "));
        if (m.partOfSpeech.isEmpty())
            parts << defs;
        else
            parts << QStringLiteral("%1 %2").arg(m.partOfSpeech, defs);
    }
    return parts.join(QStringLiteral("; "));
}

qint64 VocabStore::add(const VocabRecord &record)
{
    QSqlDatabase db = m_storage->database();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // Upsert on UNIQUE(word,lang): bump lookup_count + refresh last_seen_at, and
    // backfill any snapshot fields that were empty (DESIGN.md §8.3).
    if (auto existing = find(record.word, record.lang)) {
        QSqlQuery upd(db);
        upd.prepare(QStringLiteral("UPDATE vocab SET lookup_count = lookup_count + 1, last_seen_at = ?,"
                                   " phonetic = COALESCE(NULLIF(phonetic, ''), ?),"
                                   " dict_json = COALESCE(NULLIF(dict_json, ''), ?),"
                                   " translation = COALESCE(NULLIF(translation, ''), ?),"
                                   " context = COALESCE(NULLIF(context, ''), ?)"
                                   " WHERE id = ?"));
        upd.addBindValue(now);
        upd.addBindValue(record.phonetic);
        upd.addBindValue(dictToJsonString(record.dict));
        upd.addBindValue(record.translation);
        upd.addBindValue(record.context);
        upd.addBindValue(existing->id);
        if (!upd.exec()) {
            qCWarning(w2tCore) << "vocab upsert failed:" << upd.lastError().text();
            return -1;
        }
        return existing->id;
    }

    QSqlQuery ins(db);
    ins.prepare(
        QStringLiteral("INSERT INTO vocab (word, lang, phonetic, dict_json, translation, context, note, tags, status,"
                       " added_at, last_seen_at, lookup_count) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    ins.addBindValue(record.word);
    ins.addBindValue(record.lang);
    ins.addBindValue(record.phonetic);
    ins.addBindValue(dictToJsonString(record.dict));
    ins.addBindValue(record.translation);
    ins.addBindValue(record.context);
    ins.addBindValue(record.note);
    ins.addBindValue(tagsToJson(record.tags));
    ins.addBindValue(record.status);
    ins.addBindValue(record.addedAt > 0 ? record.addedAt : now);
    ins.addBindValue(record.lastSeenAt > 0 ? record.lastSeenAt : now);
    ins.addBindValue(record.lookupCount > 0 ? record.lookupCount : 1);
    if (!ins.exec()) {
        qCWarning(w2tCore) << "vocab insert failed:" << ins.lastError().text();
        return -1;
    }
    return ins.lastInsertId().toLongLong();
}

bool VocabStore::remove(const QString &word, const QString &lang)
{
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("DELETE FROM vocab WHERE word = ? AND lang = ?"));
    q.addBindValue(word);
    q.addBindValue(lang);
    if (!q.exec()) {
        qCWarning(w2tCore) << "vocab remove failed:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool VocabStore::removeById(qint64 id)
{
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("DELETE FROM vocab WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        qCWarning(w2tCore) << "vocab removeById failed:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool VocabStore::contains(const QString &word, const QString &lang) const
{
    return find(word, lang).has_value();
}

std::optional<VocabRecord> VocabStore::find(const QString &word, const QString &lang) const
{
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("SELECT %1 FROM vocab WHERE word = ? AND lang = ?").arg(kSelectCols));
    q.addBindValue(word);
    q.addBindValue(lang);
    if (!q.exec()) {
        qCWarning(w2tCore) << "vocab find failed:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return recordFromQuery(q);
}

QList<VocabRecord> VocabStore::list(Sort sort, int statusFilter) const
{
    const QString order = sort == Sort::LookupCountDesc ? QStringLiteral("lookup_count DESC, last_seen_at DESC")
                                                        : QStringLiteral("added_at DESC");
    QString sql = QStringLiteral("SELECT %1 FROM vocab").arg(kSelectCols);
    if (statusFilter >= 0)
        sql += QStringLiteral(" WHERE status = ?");
    sql += QStringLiteral(" ORDER BY ") + order;

    QSqlQuery q(m_storage->database());
    q.prepare(sql);
    if (statusFilter >= 0)
        q.addBindValue(statusFilter);

    QList<VocabRecord> out;
    if (!q.exec()) {
        qCWarning(w2tCore) << "vocab list failed:" << q.lastError().text();
        return out;
    }
    while (q.next())
        out << recordFromQuery(q);
    return out;
}

bool VocabStore::setStatus(qint64 id, int status)
{
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("UPDATE vocab SET status = ? WHERE id = ?"));
    q.addBindValue(status);
    q.addBindValue(id);
    if (!q.exec()) {
        qCWarning(w2tCore) << "vocab setStatus failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool VocabStore::setNote(qint64 id, const QString &note)
{
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("UPDATE vocab SET note = ? WHERE id = ?"));
    q.addBindValue(note);
    q.addBindValue(id);
    if (!q.exec()) {
        qCWarning(w2tCore) << "vocab setNote failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool VocabStore::setTags(qint64 id, const QStringList &tags)
{
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("UPDATE vocab SET tags = ? WHERE id = ?"));
    q.addBindValue(tagsToJson(tags));
    q.addBindValue(id);
    if (!q.exec()) {
        qCWarning(w2tCore) << "vocab setTags failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool VocabStore::setContext(qint64 id, const QString &context)
{
    QSqlQuery q(m_storage->database());
    q.prepare(QStringLiteral("UPDATE vocab SET context = ? WHERE id = ?"));
    q.addBindValue(context);
    q.addBindValue(id);
    if (!q.exec()) {
        qCWarning(w2tCore) << "vocab setContext failed:" << q.lastError().text();
        return false;
    }
    return true;
}

int VocabStore::count() const
{
    QSqlQuery q(m_storage->database());
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM vocab")) || !q.next())
        return 0;
    return q.value(0).toInt();
}

QString VocabStore::exportAnkiTsv() const
{
    // Fields: word | phonetic | definitions | context | note (DESIGN.md §8.3).
    // Tabs/newlines inside a field would break Anki's row parsing, so strip them.
    auto clean = [](QString s) {
        s.replace(QLatin1Char('\t'), QLatin1Char(' '));
        s.replace(QLatin1Char('\n'), QLatin1Char(' '));
        return s;
    };
    QStringList lines;
    for (const VocabRecord &r : list(Sort::AddedAtDesc)) {
        const QString defs = r.dict.has_value() ? flattenDefinitions(*r.dict) : r.translation;
        lines << QStringList {clean(r.word), clean(r.phonetic), clean(defs), clean(r.context), clean(r.note)}.join(
            QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

QString VocabStore::exportCsv() const
{
    QStringList lines;
    lines << QStringLiteral("word,lang,phonetic,definitions,translation,context,note,tags,status,lookup_count");
    for (const VocabRecord &r : list(Sort::AddedAtDesc)) {
        const QString defs = r.dict.has_value() ? flattenDefinitions(*r.dict) : QString();
        QStringList row {csvField(r.word), csvField(r.lang), csvField(r.phonetic), csvField(defs),
            csvField(r.translation), csvField(r.context), csvField(r.note), csvField(r.tags.join(QLatin1Char(' '))),
            QString::number(r.status), QString::number(r.lookupCount)};
        lines << row.join(QLatin1Char(','));
    }
    // Trailing newline so spreadsheet tools see the final row cleanly.
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString VocabStore::exportJson() const
{
    QJsonArray arr;
    for (const VocabRecord &r : list(Sort::AddedAtDesc)) {
        QJsonObject o;
        o.insert(QStringLiteral("word"), r.word);
        o.insert(QStringLiteral("lang"), r.lang);
        o.insert(QStringLiteral("phonetic"), r.phonetic);
        if (r.dict.has_value() && !r.dict->isEmpty())
            o.insert(QStringLiteral("dict"), dictToJson(*r.dict));
        o.insert(QStringLiteral("translation"), r.translation);
        o.insert(QStringLiteral("context"), r.context);
        o.insert(QStringLiteral("note"), r.note);
        QJsonArray tags;
        for (const QString &t : r.tags)
            tags.append(t);
        o.insert(QStringLiteral("tags"), tags);
        o.insert(QStringLiteral("status"), r.status);
        o.insert(QStringLiteral("added_at"), static_cast<double>(r.addedAt));
        o.insert(QStringLiteral("last_seen_at"), static_cast<double>(r.lastSeenAt));
        o.insert(QStringLiteral("lookup_count"), r.lookupCount);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), arr);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

int VocabStore::importJson(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return -1;
    const QJsonArray entries = doc.object().value(QStringLiteral("entries")).toArray();

    int touched = 0;
    for (const QJsonValue &v : entries) {
        const QJsonObject o = v.toObject();
        VocabRecord r;
        r.word = o.value(QStringLiteral("word")).toString();
        r.lang = o.value(QStringLiteral("lang")).toString();
        if (r.word.isEmpty() || r.lang.isEmpty())
            continue;
        r.phonetic = o.value(QStringLiteral("phonetic")).toString();
        if (o.contains(QStringLiteral("dict"))) {
            DictEntry d = dictFromJson(o.value(QStringLiteral("dict")).toObject());
            if (!d.isEmpty())
                r.dict = d;
        }
        r.translation = o.value(QStringLiteral("translation")).toString();
        r.context = o.value(QStringLiteral("context")).toString();
        r.note = o.value(QStringLiteral("note")).toString();
        for (const QJsonValue &t : o.value(QStringLiteral("tags")).toArray())
            r.tags << t.toString();
        r.status = o.value(QStringLiteral("status")).toInt();
        r.addedAt = static_cast<qint64>(o.value(QStringLiteral("added_at")).toDouble());
        r.lastSeenAt = static_cast<qint64>(o.value(QStringLiteral("last_seen_at")).toDouble());
        r.lookupCount = o.value(QStringLiteral("lookup_count")).toInt(1);

        // Merge on conflict: keep the row with the higher lookup_count, otherwise
        // leave the existing one untouched (DESIGN.md §8.3).
        if (auto existing = find(r.word, r.lang)) {
            if (r.lookupCount > existing->lookupCount) {
                QSqlQuery upd(m_storage->database());
                upd.prepare(QStringLiteral(
                    "UPDATE vocab SET phonetic = ?, dict_json = ?, translation = ?, context = ?, note = ?,"
                    " tags = ?, status = ?, last_seen_at = ?, lookup_count = ? WHERE id = ?"));
                upd.addBindValue(r.phonetic);
                upd.addBindValue(dictToJsonString(r.dict));
                upd.addBindValue(r.translation);
                upd.addBindValue(r.context);
                upd.addBindValue(r.note);
                upd.addBindValue(tagsToJson(r.tags));
                upd.addBindValue(r.status);
                upd.addBindValue(r.lastSeenAt > 0 ? r.lastSeenAt : QDateTime::currentSecsSinceEpoch());
                upd.addBindValue(r.lookupCount);
                upd.addBindValue(existing->id);
                if (upd.exec())
                    ++touched;
            }
            continue;
        }

        // New entry: insert directly (preserve the backup's counters, so don't go
        // through add() which would reset/increment them).
        QSqlQuery ins(m_storage->database());
        ins.prepare(QStringLiteral(
            "INSERT INTO vocab (word, lang, phonetic, dict_json, translation, context, note, tags, status,"
            " added_at, last_seen_at, lookup_count) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        ins.addBindValue(r.word);
        ins.addBindValue(r.lang);
        ins.addBindValue(r.phonetic);
        ins.addBindValue(dictToJsonString(r.dict));
        ins.addBindValue(r.translation);
        ins.addBindValue(r.context);
        ins.addBindValue(r.note);
        ins.addBindValue(tagsToJson(r.tags));
        ins.addBindValue(r.status);
        ins.addBindValue(r.addedAt > 0 ? r.addedAt : now);
        ins.addBindValue(r.lastSeenAt > 0 ? r.lastSeenAt : now);
        ins.addBindValue(r.lookupCount > 0 ? r.lookupCount : 1);
        if (ins.exec())
            ++touched;
        else
            qCWarning(w2tCore) << "vocab import insert failed:" << ins.lastError().text();
    }
    return touched;
}
