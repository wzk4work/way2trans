#pragma once

#include <QString>

class QSqlDatabase;

// SQLite connection + schema migration owner (DESIGN.md §8.1).
//
// Opens the database at ~/.local/share/way2trans/way2trans.db (or under
// XDG_DATA_HOME when set, which the tests exploit for hermetic temp databases),
// enables WAL, tightens the file permissions to 0600 (the DB may hold sensitive
// selection text), and runs the ordered user_version migrations. HistoryStore
// and VocabStore are thin CRUD layers over the connection this class owns; the
// UI reaches them through QAbstractListModel-derived models.
class Storage {
public:
    Storage();
    ~Storage();

    Storage(const Storage &) = delete;
    Storage &operator=(const Storage &) = delete;

    // Open (creating if needed) and migrate the database. Returns false if the
    // QSQLITE driver is missing or the file cannot be opened. A caller may pass
    // an explicit path (tests); the default resolves the XDG data location.
    bool open(const QString &explicitPath = {});

    // True once open() has succeeded.
    bool isOpen() const { return m_open; }

    // The resolved database file path (valid after open()).
    QString databasePath() const { return m_path; }

    // The named QSqlDatabase connection this Storage owns.
    QSqlDatabase database() const;

    // Current schema version (SQLite user_version). Exposed for tests.
    int schemaVersion() const;

    // Resolve the default database path (~/.local/share/way2trans/way2trans.db).
    static QString defaultDatabasePath();

    // The latest schema version this build migrates to.
    static int latestSchemaVersion();

private:
    bool runMigrations();

    QString m_connectionName;
    QString m_path;
    bool m_open = false;
};
