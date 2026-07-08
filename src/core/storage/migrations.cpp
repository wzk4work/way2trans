#include "migrations.h"

#include "Logging.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace w2t::storage {
namespace {

int userVersion(QSqlDatabase &db)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next())
        return 0;
    return q.value(0).toInt();
}

bool setUserVersion(QSqlDatabase &db, int version)
{
    QSqlQuery q(db);
    // PRAGMA does not accept bound parameters; version is an internal int.
    return q.exec(QStringLiteral("PRAGMA user_version = %1").arg(version));
}

// The migration to schema v1: the history + FTS + vocab tables (DESIGN.md §8.1).
const QStringList &migrationV1()
{
    static const QStringList kSteps = {
        QStringLiteral("CREATE TABLE history ("
                       "  id           INTEGER PRIMARY KEY,"
                       "  created_at   INTEGER NOT NULL,"
                       "  trigger      TEXT    NOT NULL,"
                       "  source_text  TEXT    NOT NULL,"
                       "  source_lang  TEXT, target_lang TEXT,"
                       "  engine_id    TEXT,"
                       "  result_text  TEXT,"
                       "  favorite     INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE INDEX idx_history_created ON history(created_at DESC)"),
        // Contentless-external FTS mirror of history's searchable columns.
        QStringLiteral("CREATE VIRTUAL TABLE history_fts USING fts5("
                       "  source_text, result_text, content='history', content_rowid='id')"),
        // Keep the FTS index in sync with history via triggers (the standard
        // external-content pattern from the FTS5 docs).
        QStringLiteral("CREATE TRIGGER history_ai AFTER INSERT ON history BEGIN"
                       "  INSERT INTO history_fts(rowid, source_text, result_text)"
                       "  VALUES (new.id, new.source_text, new.result_text);"
                       "END"),
        QStringLiteral("CREATE TRIGGER history_ad AFTER DELETE ON history BEGIN"
                       "  INSERT INTO history_fts(history_fts, rowid, source_text, result_text)"
                       "  VALUES ('delete', old.id, old.source_text, old.result_text);"
                       "END"),
        QStringLiteral("CREATE TRIGGER history_au AFTER UPDATE ON history BEGIN"
                       "  INSERT INTO history_fts(history_fts, rowid, source_text, result_text)"
                       "  VALUES ('delete', old.id, old.source_text, old.result_text);"
                       "  INSERT INTO history_fts(rowid, source_text, result_text)"
                       "  VALUES (new.id, new.source_text, new.result_text);"
                       "END"),
        QStringLiteral("CREATE TABLE vocab ("
                       "  id            INTEGER PRIMARY KEY,"
                       "  word          TEXT NOT NULL,"
                       "  lang          TEXT NOT NULL,"
                       "  phonetic      TEXT,"
                       "  dict_json     TEXT,"
                       "  translation   TEXT,"
                       "  context       TEXT,"
                       "  note          TEXT,"
                       "  tags          TEXT,"
                       "  status        INTEGER NOT NULL DEFAULT 0,"
                       "  added_at      INTEGER NOT NULL,"
                       "  last_seen_at  INTEGER NOT NULL,"
                       "  lookup_count  INTEGER NOT NULL DEFAULT 1,"
                       "  UNIQUE(word, lang))"),
    };
    return kSteps;
}

bool runSteps(QSqlDatabase &db, const QStringList &steps)
{
    for (const QString &sql : steps) {
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            qCWarning(w2tCore) << "migration step failed:" << q.lastError().text() << "sql:" << sql.left(60);
            return false;
        }
    }
    return true;
}

} // namespace

bool applyMigrations(QSqlDatabase &db)
{
    int version = userVersion(db);
    if (version >= kLatestSchemaVersion)
        return true;

    // Apply each pending version step inside its own transaction so a partial
    // upgrade rolls back cleanly.
    while (version < kLatestSchemaVersion) {
        const int target = version + 1;
        if (!db.transaction()) {
            qCWarning(w2tCore) << "could not begin migration transaction:" << db.lastError().text();
            return false;
        }

        bool ok = false;
        switch (target) {
        case 1:
            ok = runSteps(db, migrationV1());
            break;
        default:
            qCWarning(w2tCore) << "no migration defined for version" << target;
            ok = false;
            break;
        }

        if (ok)
            ok = setUserVersion(db, target);

        if (!ok) {
            db.rollback();
            return false;
        }
        if (!db.commit()) {
            qCWarning(w2tCore) << "migration commit failed:" << db.lastError().text();
            return false;
        }
        version = target;
        qCInfo(w2tCore) << "database migrated to schema version" << version;
    }
    return true;
}

} // namespace w2t::storage
