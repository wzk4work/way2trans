#include "VocabModel.h"

#include "LibraryService.h"

namespace {
// The first gloss across all meanings, for the compact list line.
QString firstDefinition(const VocabRecord &r)
{
    if (r.dict.has_value()) {
        for (const DictMeaning &m : r.dict->meanings) {
            if (!m.definitions.isEmpty())
                return m.definitions.first();
        }
    }
    return r.translation;
}
} // namespace

VocabModel::VocabModel(LibraryService *library, QObject *parent)
    : QAbstractListModel(parent)
    , m_library(library)
{
    reload();
}

int VocabModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant VocabModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const VocabRecord &r = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case WordRole:
        return r.word;
    case IdRole:
        return QVariant::fromValue(r.id);
    case LangRole:
        return r.lang;
    case PhoneticRole:
        return r.phonetic;
    case FirstDefinitionRole:
        return firstDefinition(r);
    case StatusRole:
        return r.status;
    case LookupCountRole:
        return r.lookupCount;
    default:
        return {};
    }
}

void VocabModel::setSort(VocabStore::Sort sort)
{
    m_sort = sort;
    reload();
}

void VocabModel::setStatusFilter(int status)
{
    m_statusFilter = status;
    reload();
}

void VocabModel::reload()
{
    beginResetModel();
    if (m_library && m_library->isReady())
        m_rows = m_library->vocab()->list(m_sort, m_statusFilter);
    else
        m_rows.clear();
    endResetModel();
}

VocabRecord VocabModel::recordAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row);
}
