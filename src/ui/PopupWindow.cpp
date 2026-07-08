#include "PopupWindow.h"

#include "EngineResultCard.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QShowEvent>
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

namespace {
// Padding reserved around the card for the drop shadow to render into. The
// translucent top-level window is this much larger than the visible card.
constexpr int kShadowMargin = 18;
} // namespace

PopupWindow::PopupWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName(QStringLiteral("popupWindow"));

    // Outer layout only reserves room for the shadow; the visible card is the
    // inner container frame, which carries the rounded background and border.
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);

    m_container = new QFrame(this);
    m_container->setObjectName(QStringLiteral("popupCard"));
    m_container->setMinimumWidth(360);
    m_container->setMaximumWidth(560);
    m_container->installEventFilter(this); // drag-to-move (see eventFilter)
    outer->addWidget(m_container);

    auto *shadow = new QGraphicsDropShadowEffect(m_container);
    shadow->setBlurRadius(28);
    shadow->setColor(QColor(0, 0, 0, 110));
    shadow->setOffset(0, 4);
    m_container->setGraphicsEffect(shadow);

    auto *root = new QVBoxLayout(m_container);
    root->setContentsMargins(12, 10, 12, 12);
    root->setSpacing(10);

    // --- title bar: pin/close top-right, then the lang badge under them ---
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(2);
    titleRow->addStretch();

    m_langPairLabel = new QLabel(m_container);
    m_langPairLabel->setObjectName(QStringLiteral("langPair"));
    titleRow->addWidget(m_langPairLabel, 0, Qt::AlignVCenter);
    titleRow->addSpacing(6);

    m_pinButton = new QToolButton(m_container);
    m_pinButton->setObjectName(QStringLiteral("iconButton"));
    m_pinButton->setCheckable(true);
    m_pinButton->setText(QStringLiteral("📌"));
    m_pinButton->setToolTip(tr("Pin (keep open on focus loss)"));
    m_pinButton->setCursor(Qt::PointingHandCursor);
    connect(m_pinButton, &QToolButton::toggled, this, &PopupWindow::setPinned);
    titleRow->addWidget(m_pinButton, 0, Qt::AlignVCenter);

    m_closeButton = new QToolButton(m_container);
    m_closeButton->setObjectName(QStringLiteral("iconButton"));
    m_closeButton->setText(QStringLiteral("✕"));
    m_closeButton->setToolTip(tr("Close (Esc)"));
    m_closeButton->setCursor(Qt::PointingHandCursor);
    connect(m_closeButton, &QToolButton::clicked, this, &PopupWindow::close);
    titleRow->addWidget(m_closeButton, 0, Qt::AlignVCenter);
    root->addLayout(titleRow);

    // --- source text: full-width boxed original text ----------------------
    // Two widgets share the slot: a read-only label for the selection flow, and
    // an editable box for the OCR flow (edit to re-translate, DESIGN.md §3.2).
    // Only one is visible per request.
    m_sourceLabel = new QLabel(m_container);
    m_sourceLabel->setObjectName(QStringLiteral("sourceText"));
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_sourceLabel);

    m_sourceEdit = new QPlainTextEdit(m_container);
    m_sourceEdit->setObjectName(QStringLiteral("sourceEdit"));
    m_sourceEdit->setToolTip(tr("Edit the recognised text, then press Ctrl+Enter to re-translate"));
    m_sourceEdit->setPlaceholderText(tr("Recognised text (editable)"));
    m_sourceEdit->setTabChangesFocus(true);
    m_sourceEdit->setMaximumHeight(96);
    m_sourceEdit->installEventFilter(this); // Ctrl+Enter -> re-translate
    m_sourceEdit->hide();
    root->addWidget(m_sourceEdit);

    // --- cards container --------------------------------------------------
    m_cardsLayout = new QVBoxLayout;
    m_cardsLayout->setSpacing(8);
    root->addLayout(m_cardsLayout);

    setStyleSheet(QStringLiteral("#popupCard {"
                                 "  background: palette(window);"
                                 "  border: 1px solid palette(mid);"
                                 "  border-radius: 14px;"
                                 "}"
                                 "#sourceText, #sourceEdit {"
                                 "  font-size: 14px;"
                                 "  color: palette(text);"
                                 "  background: palette(base);"
                                 "  border: 1px solid palette(midlight);"
                                 "  border-radius: 8px;"
                                 "  padding: 6px 8px;"
                                 "}"
                                 "#langPair {"
                                 "  color: palette(highlight-text);"
                                 "  background: palette(highlight);"
                                 "  border-radius: 8px;"
                                 "  padding: 2px 8px;"
                                 "  font-size: 11px;"
                                 "}"
                                 "QToolButton#iconButton {"
                                 "  border: none;"
                                 "  background: transparent;"
                                 "  border-radius: 6px;"
                                 "  padding: 2px 4px;"
                                 "  font-size: 13px;"
                                 "}"
                                 "QToolButton#iconButton:hover { background: palette(midlight); }"
                                 "QToolButton#iconButton:checked { background: palette(midlight); }"
                                 // The card is a transparent layout group; the body box below is the
                                 // visible rounded surface, so results aren't double-boxed.
                                 "#engineCard { background: transparent; }"
                                 "#engineName {"
                                 "  font-weight: 600;"
                                 "  color: palette(highlight);"
                                 "  font-size: 12px;"
                                 "}"
                                 "#resultBox {"
                                 "  background: palette(base);"
                                 "  border: 1px solid palette(midlight);"
                                 "  border-radius: 8px;"
                                 "}"
                                 "#resultBody { background: transparent; border: none; }"
                                 "QToolButton#cardButton {"
                                 "  border: 1px solid palette(mid);"
                                 "  border-radius: 5px;"
                                 "  padding: 1px 7px;"
                                 "  font-size: 11px;"
                                 "  color: palette(text);"
                                 "}"
                                 "QToolButton#cardButton:hover { background: palette(midlight); }"));
}

void PopupWindow::beginRequest(const TranslateRequest &request, const QStringList &engineIds, bool editableSource)
{
    clearCards();
    m_singleWord = looksLikeWord(request.text);

    // Show the read-only label or the editable box depending on the flow.
    m_sourceLabel->setVisible(!editableSource);
    m_sourceEdit->setVisible(editableSource);
    if (editableSource) {
        // Avoid emitting sourceEdited while we programmatically set the text.
        QSignalBlocker blocker(m_sourceEdit);
        m_sourceEdit->setPlainText(request.text);
    } else {
        m_sourceLabel->setText(request.text);
    }
    m_langPairLabel->setText(QStringLiteral("%1 → %2").arg(request.sourceLang, request.targetLang));

    for (const QString &id : engineIds) {
        auto *card = new EngineResultCard(id, engineDisplayName(id), m_container);
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
    // Track activation so we don't self-close on a spurious WindowDeactivate
    // that some window managers deliver before the Tool window ever gains
    // focus (which would make the popup vanish the instant it appears).
    if (event->type() == QEvent::WindowActivate)
        m_everActivated = true;

    // Close on focus loss unless pinned. A Qt::Tool window receives
    // WindowDeactivate when another window is activated. Ignore the transient
    // deactivation that happens while the user drags the window, and any that
    // arrives before the window was ever actually activated.
    if (event->type() == QEvent::WindowDeactivate && m_everActivated && !m_pinned && !m_dragging) {
        close();
        return true;
    }
    return QWidget::event(event);
}

void PopupWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Tool windows do not auto-activate on show the way Popup did; do it
    // explicitly so Esc/focus-loss handling works from the moment it appears.
    raise();
    activateWindow();
}

bool PopupWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Ctrl+Enter in the editable source box commits an edit and asks for a
    // re-translation of the new text (DESIGN.md §3.2).
    if (watched == m_sourceEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const bool isEnter = ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter;
        if (isEnter && (ke->modifiers() & Qt::ControlModifier)) {
            const QString text = m_sourceEdit->toPlainText().trimmed();
            if (!text.isEmpty())
                emit sourceEdited(text);
            return true;
        }
    }

    if (watched == m_container) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragOffset = me->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (m_dragging && (me->buttons() & Qt::LeftButton)) {
                move(me->globalPosition().toPoint() - m_dragOffset);
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            if (m_dragging) {
                m_dragging = false;
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
