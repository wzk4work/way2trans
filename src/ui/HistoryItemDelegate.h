#pragma once

#include <QStyledItemDelegate>

class QListView;

// Paints a history row Bob/Pot-style: source text over the translated result,
// time of day (and a favorite star) on the right, a hairline between rows and
// a day header ("Today" / "Yesterday" / ISO date) above the first row of each
// day group. Row widths track the viewport so long records elide instead of
// growing a horizontal scrollbar; pair with QListView::Adjust resize mode.
//
// The selected row expands in place to show the full (word-wrapped) source and
// stored translation — history viewing never re-runs a request. The view owner
// forwards selection changes to setExpandedIndex().
class HistoryItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit HistoryItemDelegate(QListView *view, QObject *parent = nullptr);

    // Expand this row (collapse with an invalid index). Emits sizeHintChanged
    // for the old and new rows so the view re-lays them out.
    void setExpandedIndex(const QModelIndex &index);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    // True when the row is the first of its day group (gets the day header).
    bool startsDayGroup(const QModelIndex &index) const;

    QListView *m_view;
    QPersistentModelIndex m_expanded; // the row rendered in full
};
