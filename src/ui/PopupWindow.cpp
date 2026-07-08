#include "PopupWindow.h"

#include "EngineResultCard.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// Display names for the built-in engine ids (kept local to avoid pulling the
// engines/ registry into the ui/ layer).
QString engineDisplayName(const QString &id)
{
    if (id == QLatin1String("google"))
        return QStringLiteral("Google");
    if (id == QLatin1String("bing"))
        return QStringLiteral("Bing");
    return id;
}

// A single "word" for dictionary-preference purposes: <= 2 whitespace tokens
// and no sentence punctuation (DESIGN.md §5.1 word scenario).
bool looksLikeWord(const QString &text)
{
    const QString t = text.trimmed();
    if (t.contains(QLatin1Char('\n')))
        return false;
    const int spaces = t.count(QLatin1Char(' '));
    return spaces <= 1 && t.size() <= 40;
}
} // namespace

PopupWindow::PopupWindow(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName(QStringLiteral("popupWindow"));
    setMinimumWidth(360);
    setMaximumWidth(560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    // --- header: source text (left) + lang pair + pin (right) -------------
    auto *headerRow = new QHBoxLayout;
    m_sourceLabel = new QLabel(this);
    m_sourceLabel->setObjectName(QStringLiteral("sourceText"));
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerRow->addWidget(m_sourceLabel, 1);

    auto *headerRight = new QVBoxLayout;
    m_pinButton = new QToolButton(this);
    m_pinButton->setCheckable(true);
    m_pinButton->setText(QStringLiteral("📌"));
    m_pinButton->setToolTip(tr("Pin (keep open on focus loss)"));
    connect(m_pinButton, &QToolButton::toggled, this, &PopupWindow::setPinned);
    headerRight->addWidget(m_pinButton, 0, Qt::AlignRight);

    m_langPairLabel = new QLabel(this);
    m_langPairLabel->setObjectName(QStringLiteral("langPair"));
    headerRight->addWidget(m_langPairLabel, 0, Qt::AlignRight);
    headerRow->addLayout(headerRight);
    root->addLayout(headerRow);

    // --- cards container --------------------------------------------------
    m_cardsLayout = new QVBoxLayout;
    m_cardsLayout->setSpacing(6);
    root->addLayout(m_cardsLayout);

    // A soft stylesheet; theme-aware polish arrives with the settings work.
    setStyleSheet(QStringLiteral("#popupWindow { }"
                                 "#sourceText { font-size: 13px; }"
                                 "#langPair { color: gray; font-size: 11px; }"
                                 "#engineCard { background: palette(base); border-radius: 8px; }"
                                 "#engineName { font-weight: bold; color: palette(highlight); }"));
}

void PopupWindow::beginRequest(const TranslateRequest &request, const QStringList &engineIds)
{
    clearCards();
    m_singleWord = looksLikeWord(request.text);

    m_sourceLabel->setText(request.text);
    m_langPairLabel->setText(QStringLiteral("%1 → %2").arg(request.sourceLang, request.targetLang));

    for (const QString &id : engineIds) {
        auto *card = new EngineResultCard(id, engineDisplayName(id), this);
        connect(card, &EngineResultCard::retryRequested, this, &PopupWindow::retryRequested);
        m_cards.insert(id, card);
        m_cardsLayout->addWidget(card);
    }
    adjustSize();
}

void PopupWindow::applyResult(const TranslateResult &result)
{
    auto it = m_cards.constFind(result.engineId);
    if (it == m_cards.constEnd())
        return;
    it.value()->showResult(result, m_singleWord);
    adjustSize();
}

void PopupWindow::clearCards()
{
    for (EngineResultCard *card : std::as_const(m_cards))
        card->deleteLater();
    m_cards.clear();
}

void PopupWindow::setPinned(bool pinned)
{
    m_pinned = pinned;
}

void PopupWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool PopupWindow::event(QEvent *event)
{
    // Close on focus loss unless pinned. A Qt::Popup window receives
    // WindowDeactivate when the user clicks elsewhere.
    if (event->type() == QEvent::WindowDeactivate && !m_pinned) {
        close();
        return true;
    }
    return QWidget::event(event);
}

void PopupWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // Draw the rounded card background ourselves since the window is translucent.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);
    p.fillPath(path, palette().window());
    p.setPen(palette().mid().color());
    p.drawPath(path);
}
