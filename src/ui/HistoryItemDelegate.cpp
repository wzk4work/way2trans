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

constexpr int kWrapFlags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;

// Height of `text` word-wrapped into the given width.
int wrappedHeight(const QFontMetrics &fm, int width, const QString &text)
{
    if (text.isEmpty())
        return 0;
    return fm.boundingRect(QRect(0, 0, width, 100000), kWrapFlags, text).height();
}
} // namespace

HistoryItemDelegate::HistoryItemDelegate(QListView *view, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_view(view)
{ }

void HistoryItemDelegate::setExpandedIndex(const QModelIndex &index)
{
    if (m_expanded == index)
        return;
    const QModelIndex old = m_expanded;
    m_expanded = index;
    if (old.isValid())
        emit sizeHintChanged(old);
    if (index.isValid())
        emit sizeHintChanged(index);
}

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

    // The expanded (selected) row shows its full text: a quiet backdrop with
    // the normal text colours reads better for paragraphs than a highlight
    // slab, and selection == expansion so no separate selected look is needed.
    const bool expanded = m_expanded.isValid() && QModelIndex(m_expanded) == index;

    if (expanded || selected || option.state.testFlag(QStyle::State_MouseOver)) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(pal.color((selected && !expanded) ? QPalette::Highlight : QPalette::Midlight));
        painter->drawRoundedRect(rect.adjusted(2, 1, -2, -1), 6, 6);
    }

    const bool highlighted = selected && !expanded;
    const QColor primary = pal.color(highlighted ? QPalette::HighlightedText : QPalette::Text);
    QColor secondary = pal.color(highlighted ? QPalette::HighlightedText : QPalette::PlaceholderText);
    if (highlighted)
        secondary.setAlphaF(0.75);

    const QRect content = rect.adjusted(kHPad, kVPad, -kHPad, -kVPad);

    // Meta block, right-aligned: favorite star + time of day.
    const auto created = QDateTime::fromSecsSinceEpoch(index.data(HistoryModel::CreatedAtRole).toLongLong());
    QString meta = QLocale().toString(created.time(), QLocale::ShortFormat);
    if (index.data(HistoryModel::FavoriteRole).toBool())
        meta.prepend(QStringLiteral("★ "));
    const int metaWidth = metaFm.horizontalAdvance(meta);
    painter->setFont(metaFont);
    painter->setPen(secondary);
    painter->drawText(QRect(content.right() - metaWidth, content.top(), metaWidth, textFm.height()),
        Qt::AlignRight | Qt::AlignVCenter, meta);

    if (expanded) {
        // Meta on its own line, then the full wrapped source and translation —
        // straight from the store, no re-request (DESIGN.md §8.2).
        const QString source = index.data(HistoryModel::SourceTextRole).toString();
        const QString result = index.data(HistoryModel::ResultTextRole).toString();
        int y = content.top() + metaFm.height() + kLineGap;
        painter->setFont(option.font);
        painter->setPen(primary);
        const int srcH = wrappedHeight(textFm, content.width(), source);
        painter->drawText(QRect(content.left(), y, content.width(), srcH), kWrapFlags, source);
        y += srcH + (srcH > 0 ? kLineGap : 0);
        painter->setPen(secondary);
        const int resH = wrappedHeight(textFm, content.width(), result);
        painter->drawText(QRect(content.left(), y, content.width(), resH), kWrapFlags, result);
    } else {
        // Line 1: source text, elided before the meta block.
        const int sourceWidth = content.width() - metaWidth - kMetaGap;
        painter->setFont(option.font);
        painter->setPen(primary);
        painter->drawText(QRect(content.left(), content.top(), sourceWidth, textFm.height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            textFm.elidedText(
                flattened(index.data(HistoryModel::SourceTextRole).toString()), Qt::ElideRight, sourceWidth));

        // Line 2: the translated result, in the secondary color.
        painter->setPen(secondary);
        const QRect resultRect(
            content.left(), content.top() + textFm.height() + kLineGap, content.width(), textFm.height());
        painter->drawText(resultRect, Qt::AlignLeft | Qt::AlignVCenter,
            textFm.elidedText(
                flattened(index.data(HistoryModel::ResultTextRole).toString()), Qt::ElideRight, resultRect.width()));
    }

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
    // Track the viewport width so rows span the list exactly and long text
    // elides instead of feeding a horizontal scrollbar.
    const int width = m_view ? m_view->viewport()->width() : option.rect.width();

    int height;
    if (m_expanded.isValid() && QModelIndex(m_expanded) == index) {
        // Must mirror the expanded paint() layout: meta line + wrapped texts.
        const int contentWidth = width - 2 * kHPad;
        const int srcH = wrappedHeight(textFm, contentWidth, index.data(HistoryModel::SourceTextRole).toString());
        const int resH = wrappedHeight(textFm, contentWidth, index.data(HistoryModel::ResultTextRole).toString());
        height = 2 * kVPad + metaFm.height() + kLineGap + srcH + (srcH > 0 ? kLineGap : 0) + resH + 1;
    } else {
        height = 2 * kVPad + 2 * textFm.height() + kLineGap + 1; // +1: separator hairline
    }
    if (startsDayGroup(index))
        height += kHeaderTopGap + metaFm.height() + kHeaderBottomGap;
    return QSize(width, height);
}
