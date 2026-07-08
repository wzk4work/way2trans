#pragma once

class QSqlDatabase;

namespace w2t::storage {

// The schema version this build ships. Bump it and add a case in
// applyMigrations() whenever the schema changes (DESIGN.md §8.1).
inline constexpr int kLatestSchemaVersion = 1;

// Migrate an open database from its current user_version up to
// kLatestSchemaVersion, running each ordered step inside a transaction. Returns
// false if any step fails (the failing step's transaction is rolled back).
bool applyMigrations(QSqlDatabase &db);

} // namespace w2t::storage
