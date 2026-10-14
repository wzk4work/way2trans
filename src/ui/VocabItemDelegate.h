#pragma once

#include <QStyledItemDelegate>

class QListView;

// Paints a vocabulary row Bob/Pot-style: the word (with its phonetic
// alongside) over the first definition, status + lookup count right-aligned,
// and a hairline between rows. Row widths track the viewport so long entries
// elide instead of growing a horizontal scrollbar; pair with
// QListView::Adjust resize mode.
class VocabItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit VocabItemDelegate(QListView *view, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QListView *m_view;
};
