#include "HistoryModel.h"

#include "LibraryService.h"

#include <QDateTime>

namespace {
// A human day label used to group rows in the list (DESIGN.md §8.2).
QString dayLabel(qint64 epoch)
{
    const QDate date = QDateTime::fromSecsSinceEpoch(epoch).date();
    const QDate today = QDate::currentDate();
    if (date == today)
        return QObject::tr("Today");
    if (date == today.addDays(-1))
        return QObject::tr("Yesterday");
    return date.toString(Qt::ISODate);
}
} // namespace

HistoryModel::HistoryModel(LibraryService *library, QObject *parent)
    : QAbstractListModel(parent)
    , m_library(library)
{
    reload();
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const HistoryRecord &r = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case SourceTextRole:
        return r.sourceText;
    case ResultTextRole:
        return r.resultText;
    case IdRole:
        return QVariant::fromValue(r.id);
    case TriggerRole:
        return r.trigger;
    case CreatedAtRole:
        return QVariant::fromValue(r.createdAt);
    case FavoriteRole:
        return r.favorite;
    case DateGroupRole:
        return dayLabel(r.createdAt);
    default:
        return {};
    }
}

void HistoryModel::reload()
{
    beginResetModel();
    if (m_library && m_library->isReady())
        m_rows = m_query.isEmpty() ? m_library->history()->recent(500) : m_library->history()->search(m_query, 500);
    else
        m_rows.clear();
    endResetModel();
}

void HistoryModel::search(const QString &query)
{
    m_query = query.trimmed();
    reload();
}

HistoryRecord HistoryModel::recordAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row);
}
