#include "Storage.h"

#include "Logging.h"
#include "migrations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <atomic>

namespace {
// Each Storage gets a uniquely named connection so multiple instances (e.g. a
// test DB and the live DB) never collide inside Qt's global connection pool.
QString makeConnectionName()
{
    static std::atomic_int counter {0};
    return QStringLiteral("way2trans_storage_%1").arg(counter.fetch_add(1));
}
} // namespace

Storage::Storage()
    : m_connectionName(makeConnectionName())
{ }

Storage::~Storage()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, /*open=*/false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

QString Storage::defaultDatabasePath()
{
    // ~/.local/share/way2trans/way2trans.db (respects XDG_DATA_HOME) — DESIGN §8.1.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(QStringLiteral("way2trans/way2trans.db"));
}

int Storage::latestSchemaVersion()
{
    return w2t::storage::kLatestSchemaVersion;
}

QSqlDatabase Storage::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

bool Storage::open(const QString &explicitPath)
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        qCWarning(w2tCore) << "QSQLITE driver not available; history/vocabulary disabled";
        return false;
    }

    m_path = explicitPath.isEmpty() ? defaultDatabasePath() : explicitPath;
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.absolutePath())) {
        qCWarning(w2tCore) << "could not create data directory" << info.absolutePath();
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_path);
    if (!db.open()) {
        qCWarning(w2tCore) << "could not open database" << m_path << ":" << db.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    // The database may hold sensitive selection text; keep it owner-only (0600).
    QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner);

    // WAL for durability without heavy locking; foreign keys on for safety.
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    if (!w2t::storage::applyMigrations(db)) {
        qCWarning(w2tCore) << "schema migration failed for" << m_path;
        db.close();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    m_open = true;
    qCInfo(w2tCore) << "storage open at" << m_path << "schema" << schemaVersion();
    return true;
}

int Storage::schemaVersion() const
{
    QSqlQuery q(database());
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next())
        return -1;
    return q.value(0).toInt();
}
