#pragma once

#include "storage/VocabStore.h"

#include <QAbstractListModel>
#include <QList>

class LibraryService;

// List model over the vocabulary store for the MainWindow vocabulary tab
// (DESIGN.md §7/§8.3). Holds a snapshot; reload() refreshes with the current
// sort + status filter.
class VocabModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        WordRole,
        LangRole,
        PhoneticRole,
        FirstDefinitionRole,
        StatusRole,
        LookupCountRole,
    };

    explicit VocabModel(LibraryService *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setSort(VocabStore::Sort sort);
    void setStatusFilter(int status); // < 0 = all
    void reload();

    VocabRecord recordAt(int row) const;

private:
    LibraryService *m_library;
    QList<VocabRecord> m_rows;
    VocabStore::Sort m_sort = VocabStore::Sort::LookupCountDesc;
    int m_statusFilter = -1;
};
