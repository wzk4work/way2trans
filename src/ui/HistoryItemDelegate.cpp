#include "HistoryItemDelegate.h"

#include "HistoryModel.h"

#include <QDateTime>
#include <QListView>
#include <QLocale>
#include <QPainter>

namespace {
constexpr int kHPad = 10; // horizontal inset of text and separators
constexpr int kVPad = 8; // vertical padding around the two text lines
constexpr int kLineGap = 3; // gap between the source and result lines
constexpr int kMetaGap = 12; // gap between the source line and the meta block
constexpr int kHeaderTopGap = 10;
constexpr int kHeaderBottomGap = 4;

QFont metaFontFor(const QFont &base)
{
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * 0.85);
    return font;
}

// Collapse newlines/runs of whitespace so a multi-line record reads as one line.
QString flattened(QString text)
{
    return text.replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified();
}
} // namespace

HistoryItemDelegate::HistoryItemDelegate(QListView *view, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_view(view)
{ }

bool HistoryItemDelegate::startsDayGroup(const QModelIndex &index) const
{
    if (index.row() == 0)
        return true;
    const QModelIndex prev = index.siblingAtRow(index.row() - 1);
    return prev.data(HistoryModel::DateGroupRole) != index.data(HistoryModel::DateGroupRole);
}

void HistoryItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QPalette &pal = option.palette;
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const QFontMetrics textFm(option.font);
    const QFont metaFont = metaFontFor(option.font);
    const QFontMetrics metaFm(metaFont);

    QRect rect = option.rect;

    if (startsDayGroup(index)) {
        painter->setFont(metaFont);
        painter->setPen(pal.color(QPalette::PlaceholderText));
        painter->drawText(
            QRect(rect.left() + kHPad, rect.top() + kHeaderTopGap, rect.width() - 2 * kHPad, metaFm.height()),
            Qt::AlignLeft | Qt::AlignVCenter, index.data(HistoryModel::DateGroupRole).toString());
        rect.setTop(rect.top() + kHeaderTopGap + metaFm.height() + kHeaderBottomGap);
    }

    if (selected || option.state.testFlag(QStyle::State_MouseOver)) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(pal.color(selected ? QPalette::Highlight : QPalette::Midlight));
        painter->drawRoundedRect(rect.adjusted(2, 1, -2, -1), 6, 6);
    }

    const QColor primary = pal.color(selected ? QPalette::HighlightedText : QPalette::Text);
    QColor secondary = pal.color(selected ? QPalette::HighlightedText : QPalette::PlaceholderText);
    if (selected)
        secondary.setAlphaF(0.75);

    const QRect content = rect.adjusted(kHPad, kVPad, -kHPad, -kVPad);

    // Meta block, right-aligned on the first line: favorite star + time of day.
    const auto created = QDateTime::fromSecsSinceEpoch(index.data(HistoryModel::CreatedAtRole).toLongLong());
    QString meta = QLocale().toString(created.time(), QLocale::ShortFormat);
    if (index.data(HistoryModel::FavoriteRole).toBool())
        meta.prepend(QStringLiteral("★ "));
    const int metaWidth = metaFm.horizontalAdvance(meta);
    painter->setFont(metaFont);
    painter->setPen(secondary);
    painter->drawText(QRect(content.right() - metaWidth, content.top(), metaWidth, textFm.height()),
        Qt::AlignRight | Qt::AlignVCenter, meta);

    // Line 1: source text, elided before the meta block.
    const int sourceWidth = content.width() - metaWidth - kMetaGap;
    painter->setFont(option.font);
    painter->setPen(primary);
    painter->drawText(QRect(content.left(), content.top(), sourceWidth, textFm.height()),
        Qt::AlignLeft | Qt::AlignVCenter,
        textFm.elidedText(flattened(index.data(HistoryModel::SourceTextRole).toString()), Qt::ElideRight, sourceWidth));

    // Line 2: the translated result, in the secondary color.
    painter->setPen(secondary);
    const QRect resultRect(
        content.left(), content.top() + textFm.height() + kLineGap, content.width(), textFm.height());
    painter->drawText(resultRect, Qt::AlignLeft | Qt::AlignVCenter,
        textFm.elidedText(
            flattened(index.data(HistoryModel::ResultTextRole).toString()), Qt::ElideRight, resultRect.width()));

    // Hairline separator below rows followed by another row of the same day.
    const QModelIndex next = index.siblingAtRow(index.row() + 1);
    if (next.isValid() && next.data(HistoryModel::DateGroupRole) == index.data(HistoryModel::DateGroupRole)) {
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(pal.color(QPalette::Midlight));
        painter->drawLine(rect.left() + kHPad, rect.bottom(), rect.right() - kHPad, rect.bottom());
    }

    painter->restore();
}

QSize HistoryItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QFontMetrics textFm(option.font);
    const QFontMetrics metaFm(metaFontFor(option.font));
    int height = 2 * kVPad + 2 * textFm.height() + kLineGap + 1; // +1: separator hairline
    if (startsDayGroup(index))
        height += kHeaderTopGap + metaFm.height() + kHeaderBottomGap;
    // Track the viewport width so rows span the list exactly and long text
    // elides instead of feeding a horizontal scrollbar.
    const int width = m_view ? m_view->viewport()->width() : option.rect.width();
    return QSize(width, height);
}
