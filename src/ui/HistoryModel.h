#pragma once

#include "storage/HistoryStore.h"

#include <QAbstractListModel>
#include <QList>

class LibraryService;

// List model over the history store for the MainWindow history tab
// (DESIGN.md §7/§8.2). Holds a snapshot of rows; call reload()/search() to
// refresh from the store. Rows are newest-first.
class HistoryModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        SourceTextRole,
        ResultTextRole,
        TriggerRole,
        CreatedAtRole,
        FavoriteRole,
        DateGroupRole, // human day label ("Today"/"Yesterday"/ISO date) for grouping
    };

    explicit HistoryModel(LibraryService *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    // Refresh from the store: all recent rows, or FTS matches when query is set.
    void reload();
    void search(const QString &query);

    // The record at a row (for context menus / re-translation).
    HistoryRecord recordAt(int row) const;

private:
    LibraryService *m_library;
    QList<HistoryRecord> m_rows;
    QString m_query;
};
