#include "VocabItemDelegate.h"

#include "VocabModel.h"

#include <QListView>
#include <QPainter>

namespace {
constexpr int kHPad = 10; // horizontal inset of text and separators
constexpr int kVPad = 8; // vertical padding around the two text lines
constexpr int kLineGap = 3; // gap between the word and definition lines
constexpr int kMetaGap = 12; // gap between the word line and the meta block
constexpr int kPhoneticGap = 8; // gap between the word and its phonetic

QFont metaFontFor(const QFont &base)
{
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * 0.85);
    return font;
}

// Collapse newlines/runs of whitespace so a multi-line gloss reads as one line.
QString flattened(QString text)
{
    return text.replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified();
}
} // namespace

VocabItemDelegate::VocabItemDelegate(QListView *view, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_view(view)
{ }

void VocabItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QPalette &pal = option.palette;
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    QFont wordFont = option.font;
    wordFont.setBold(true);
    const QFontMetrics textFm(option.font);
    const QFontMetrics wordFm(wordFont);
    const QFont metaFont = metaFontFor(option.font);
    const QFontMetrics metaFm(metaFont);

    const QRect rect = option.rect;

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

    // Meta block, right-aligned on the first line: status and lookup count.
    QString status;
    switch (index.data(VocabModel::StatusRole).toInt()) {
    case 1:
        status = tr("Familiar");
        break;
    case 2:
        status = tr("Mastered");
        break;
    default:
        status = tr("New");
        break;
    }
    const QString meta
        = status + QStringLiteral(" · ") + tr("%n lookup(s)", nullptr, index.data(VocabModel::LookupCountRole).toInt());
    const int metaWidth = metaFm.horizontalAdvance(meta);
    painter->setFont(metaFont);
    painter->setPen(secondary);
    painter->drawText(QRect(content.right() - metaWidth, content.top(), metaWidth, wordFm.height()),
        Qt::AlignRight | Qt::AlignVCenter, meta);

    // Line 1: the word in bold, its phonetic alongside in the quiet color.
    const int line1Width = content.width() - metaWidth - kMetaGap;
    const QString word
        = wordFm.elidedText(flattened(index.data(VocabModel::WordRole).toString()), Qt::ElideRight, line1Width);
    painter->setFont(wordFont);
    painter->setPen(primary);
    painter->drawText(
        QRect(content.left(), content.top(), line1Width, wordFm.height()), Qt::AlignLeft | Qt::AlignVCenter, word);

    const QString phonetic = index.data(VocabModel::PhoneticRole).toString();
    const int phoneticLeft = content.left() + wordFm.horizontalAdvance(word) + kPhoneticGap;
    const int phoneticWidth = content.left() + line1Width - phoneticLeft;
    if (!phonetic.isEmpty() && phoneticWidth > 0) {
        painter->setFont(metaFont);
        painter->setPen(secondary);
        painter->drawText(QRect(phoneticLeft, content.top(), phoneticWidth, wordFm.height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            metaFm.elidedText(QStringLiteral("/%1/").arg(flattened(phonetic)), Qt::ElideRight, phoneticWidth));
    }

    // Line 2: the first definition (or the plain translation), quiet color.
    painter->setFont(option.font);
    painter->setPen(secondary);
    const QRect defRect(content.left(), content.top() + wordFm.height() + kLineGap, content.width(), textFm.height());
    painter->drawText(defRect, Qt::AlignLeft | Qt::AlignVCenter,
        textFm.elidedText(
            flattened(index.data(VocabModel::FirstDefinitionRole).toString()), Qt::ElideRight, defRect.width()));

    // Hairline separator below every row but the last.
    if (index.siblingAtRow(index.row() + 1).isValid()) {
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(pal.color(QPalette::Midlight));
        painter->drawLine(rect.left() + kHPad, rect.bottom(), rect.right() - kHPad, rect.bottom());
    }

    painter->restore();
}

QSize VocabItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(index);
    QFont wordFont = option.font;
    wordFont.setBold(true);
    const QFontMetrics textFm(option.font);
    const QFontMetrics wordFm(wordFont);
    const int height = 2 * kVPad + wordFm.height() + kLineGap + textFm.height() + 1; // +1: separator hairline
    // Track the viewport width so rows span the list exactly and long text
    // elides instead of feeding a horizontal scrollbar.
    const int width = m_view ? m_view->viewport()->width() : option.rect.width();
    return QSize(width, height);
}
