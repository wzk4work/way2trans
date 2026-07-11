#include "PopupWindow.h"

#include "CardStyle.h"
#include "EngineResultCard.h"
#include "LangOptions.h"
#include "Settings.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
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
    m_titleRow = new QHBoxLayout;
    auto *titleRow = m_titleRow;
    titleRow->setSpacing(2);
    titleRow->addStretch();

    m_langPairLabel = new QLabel(m_container);
    m_langPairLabel->setObjectName(QStringLiteral("langPair"));
    titleRow->addWidget(m_langPairLabel, 0, Qt::AlignVCenter);
    titleRow->addSpacing(4);

    // Target-language quick switch (Bob-style): writes the global rule and
    // asks for a re-run. activated() only fires on user picks, so beginRequest
    // can set the current item without a signal-blocking dance.
    m_targetCombo = new QComboBox(m_container);
    m_targetCombo->setObjectName(QStringLiteral("popupLangCombo"));
    w2t::ui::populateTargetRuleCombo(m_targetCombo);
    m_targetCombo->setCursor(Qt::PointingHandCursor);
    m_targetCombo->setToolTip(tr("Target language — switching re-translates"));
    connect(m_targetCombo, &QComboBox::activated, this, [this]() {
        Settings::instance().setValue(QStringLiteral("general/targetLangRule"), m_targetCombo->currentData());
        emit targetLangChanged();
    });
    titleRow->addWidget(m_targetCombo, 0, Qt::AlignVCenter);
    titleRow->addSpacing(6);

    // Vocabulary star: shown only for single-word lookups (DESIGN.md §8.3).
    m_starButton = new QToolButton(m_container);
    m_starButton->setObjectName(QStringLiteral("iconButton"));
    m_starButton->setCheckable(true);
    m_starButton->setText(QStringLiteral("☆"));
    m_starButton->setToolTip(tr("Add to vocabulary (Ctrl+S)"));
    m_starButton->setCursor(Qt::PointingHandCursor);
    m_starButton->hide();
    connect(m_starButton, &QToolButton::toggled, this, &PopupWindow::onStarToggled);
    titleRow->addWidget(m_starButton, 0, Qt::AlignVCenter);

    m_pinButton = new QToolButton(m_container);
    m_pinButton->setObjectName(QStringLiteral("iconButton"));
    m_pinButton->setCheckable(true);
    m_pinButton->setText(QStringLiteral("📌"));
    m_pinButton->setToolTip(tr("Pin (keep open on focus loss, Ctrl+P)"));
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

    // Keyboard access to the title-bar actions (DESIGN.md §7, Bob-style):
    // window-context shortcuts, so they fire with any child focused.
    auto *pinShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_P), this);
    connect(pinShortcut, &QShortcut::activated, m_pinButton, &QToolButton::toggle);
    auto *starShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this);
    connect(starShortcut, &QShortcut::activated, this, [this]() {
        if (m_starButton->isVisible())
            m_starButton->toggle();
    });
    auto *copyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), this);
    connect(copyShortcut, &QShortcut::activated, this, [this]() {
        if (!m_firstResultText.isEmpty())
            QApplication::clipboard()->setText(m_firstResultText);
    });

    // --- scrollable content: source text + result cards --------------------
    // The popup's height is capped to a fraction of the screen
    // (adjustPopupSize); everything below the title bar lives in this scroll
    // area, so overflowing results scroll instead of being clipped off-screen.
    m_scroll = new QScrollArea(m_container);
    m_scroll->setObjectName(QStringLiteral("popupScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Track the content's size hint, so the compact (short-result) popup still
    // hugs its content instead of the QScrollArea default hint.
    m_scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);

    auto *content = new QWidget(m_scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);

    // --- source text: full-width boxed original text ----------------------
    // Two widgets share the slot: a read-only label for the selection flow, and
    // an editable box for the OCR flow (edit to re-translate, DESIGN.md §3.2).
    // Only one is visible per request.
    m_sourceLabel = new QLabel(content);
    m_sourceLabel->setObjectName(QStringLiteral("sourceText"));
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    contentLayout->addWidget(m_sourceLabel);

    // The rounded surface is a QFrame and the text edit inside stays flat and
    // padding-free (same pattern as #inputCard / #resultBox): a stylesheet
    // box model directly on a scrollable widget leaves blit residue of the
    // previous frame in the padding strip when the content scrolls.
    m_sourceCard = new QFrame(content);
    m_sourceCard->setObjectName(QStringLiteral("sourceCard"));
    auto *sourceCardLayout = new QVBoxLayout(m_sourceCard);
    sourceCardLayout->setContentsMargins(8, 6, 8, 6);

    m_sourceEdit = new QPlainTextEdit(m_sourceCard);
    m_sourceEdit->setObjectName(QStringLiteral("sourceEdit"));
    m_sourceEdit->setFrameShape(QFrame::NoFrame);
    m_sourceEdit->viewport()->setAutoFillBackground(false);
    m_sourceEdit->setToolTip(tr("Edit the recognised text, then press Ctrl+Enter to re-translate"));
    m_sourceEdit->setPlaceholderText(tr("Recognised text (editable)"));
    m_sourceEdit->setTabChangesFocus(true);
    m_sourceEdit->setMaximumHeight(96);
    m_sourceEdit->installEventFilter(this); // Ctrl+Enter -> re-translate
    sourceCardLayout->addWidget(m_sourceEdit);
    m_sourceCard->hide();
    contentLayout->addWidget(m_sourceCard);

    // --- cards container --------------------------------------------------
    m_cardsLayout = new QVBoxLayout;
    m_cardsLayout->setSpacing(8);
    contentLayout->addLayout(m_cardsLayout);
    // Keeps the cards top-aligned if the viewport ever ends up taller than the
    // content (the stretch is what absorbs the extra space, not card spacing).
    contentLayout->addStretch();

    m_scroll->setWidget(content);
    root->addWidget(m_scroll, 1);

    // adjustPopupSize() measures via height-for-width hints, which can be a
    // text line short for the wrapped source label; the resulting scrollbar
    // materialises only after the async window resize round-trips. When that
    // happens, its range is the exact shortfall — absorb it while the popup
    // still fits under the screen cap instead of scrolling a few pixels.
    // Grow-only, so it cannot oscillate with the shrinking in adjustPopupSize.
    connect(m_scroll->verticalScrollBar(), &QAbstractSlider::rangeChanged, this, [this](int, int max) {
        const QScreen *scr = screen();
        const int cap = scr ? int(scr->availableGeometry().height() * 0.7) : 600;
        if (max > 0 && height() + max <= cap)
            setFixedHeight(height() + max);
    });

    setStyleSheet(QStringLiteral("#popupCard {"
                                 "  background: palette(window);"
                                 "  border: 1px solid palette(mid);"
                                 "  border-radius: 14px;"
                                 "}"
                                 "#popupScroll { background: transparent; border: none; }"
                                 "#popupScroll > QWidget > QWidget { background: transparent; }"
                                 "#sourceText, #sourceCard {"
                                 "  background: palette(base);"
                                 "  border: 1px solid palette(midlight);"
                                 "  border-radius: 8px;"
                                 "}"
                                 "#sourceText {"
                                 "  font-size: 14px;"
                                 "  color: palette(text);"
                                 "  padding: 6px 8px;"
                                 "}"
                                 "#sourceEdit {"
                                 "  font-size: 14px;"
                                 "  color: palette(text);"
                                 "  background: transparent;"
                                 "  border: none;"
                                 "  padding: 0;"
                                 "}"
                                 "#langPair {"
                                 "  color: palette(highlight-text);"
                                 "  background: palette(highlight);"
                                 "  border-radius: 8px;"
                                 "  padding: 2px 8px;"
                                 "  font-size: 11px;"
                                 "}"
                                 // Compact combo; a box model on QComboBox flips
                                 // Qt to menu-style popup rendering, so the
                                 // ::item:selected rule is mandatory (AGENTS.md).
                                 "QComboBox#popupLangCombo {"
                                 "  background: palette(base);"
                                 "  border: 1px solid palette(midlight);"
                                 "  border-radius: 6px;"
                                 "  padding: 1px 6px;"
                                 "  font-size: 11px;"
                                 "}"
                                 "QComboBox#popupLangCombo QAbstractItemView {"
                                 "  background: palette(base);"
                                 "  border: 1px solid palette(midlight);"
                                 "}"
                                 "QComboBox#popupLangCombo::item:selected {"
                                 "  background: palette(highlight);"
                                 "  color: palette(highlighted-text);"
                                 "}"
                                 "QToolButton#iconButton {"
                                 "  border: none;"
                                 "  background: transparent;"
                                 "  border-radius: 6px;"
                                 "  padding: 2px 4px;"
                                 "  font-size: 13px;"
                                 "}"
                                 "QToolButton#iconButton:hover { background: palette(midlight); }"
                                 "QToolButton#iconButton:checked { background: palette(midlight); }")
        + w2t::ui::cardStyleSheet());
}

void PopupWindow::beginRequest(const TranslateRequest &request, const QStringList &engineIds,
    const QHash<QString, QString> &engineNames, bool editableSource)
{
    clearCards();
    m_singleWord = looksLikeWord(request.text);
    m_firstResultText.clear();

    // The star is a single-word affordance; reset it to the hollow state until
    // the app reports whether this word is already saved (setSaved).
    m_currentWord = request.text.trimmed();
    m_currentContext = request.text;
    m_starButton->setVisible(m_singleWord);
    {
        QSignalBlocker blocker(m_starButton);
        m_starButton->setChecked(false);
        m_starButton->setText(QStringLiteral("☆"));
    }

    // Show the read-only label or the editable box depending on the flow.
    m_sourceLabel->setVisible(!editableSource);
    m_sourceCard->setVisible(editableSource);
    if (editableSource) {
        // Avoid emitting sourceEdited while we programmatically set the text.
        QSignalBlocker blocker(m_sourceEdit);
        m_sourceEdit->setPlainText(request.text);
    } else {
        m_sourceLabel->setText(request.text);
    }
    // Source badge + the resolved target in the switchable combo.
    m_langPairLabel->setText(QStringLiteral("%1 →").arg(request.sourceLang));
    w2t::ui::selectTargetRule(m_targetCombo, QStringLiteral("to:") + request.targetLang);

    for (const QString &id : engineIds) {
        auto *card = new EngineResultCard(id, engineNames.value(id, id), m_container);
        connect(card, &EngineResultCard::retryRequested, this, &PopupWindow::retryRequested);
        m_cards.insert(id, card);
        m_cardsLayout->addWidget(card);
    }
    m_scroll->verticalScrollBar()->setValue(0); // fresh request reads from the top
    adjustPopupSize();
}

void PopupWindow::applyResult(const TranslateResult &result)
{
    auto it = m_cards.constFind(result.engineId);
    if (it == m_cards.constEnd())
        return;
    if (result.ok() && m_firstResultText.isEmpty())
        m_firstResultText = result.translatedText;
    it.value()->showResult(result, m_singleWord);
    scheduleAdjust();
}

void PopupWindow::applyPartial(const QString &engineId, const QString &delta)
{
    auto it = m_cards.constFind(engineId);
    if (it == m_cards.constEnd())
        return;
    it.value()->appendStreamDelta(delta);
    scheduleAdjust();
}

void PopupWindow::setSaved(bool saved)
{
    if (!m_singleWord)
        return;
    QSignalBlocker blocker(m_starButton); // reflect state without re-triggering
    m_starButton->setChecked(saved);
    m_starButton->setText(saved ? QStringLiteral("★") : QStringLiteral("☆"));
    m_starButton->setToolTip(saved ? tr("Remove from vocabulary (Ctrl+S)") : tr("Add to vocabulary (Ctrl+S)"));
}

void PopupWindow::onStarToggled(bool checked)
{
    if (m_currentWord.isEmpty())
        return;
    m_starButton->setText(checked ? QStringLiteral("★") : QStringLiteral("☆"));
    m_starButton->setToolTip(checked ? tr("Remove from vocabulary (Ctrl+S)") : tr("Add to vocabulary (Ctrl+S)"));
    if (checked)
        emit saveRequested(m_currentWord, m_currentContext);
    else
        emit unsaveRequested(m_currentWord);
}

void PopupWindow::adjustPopupSize()
{
    // Cap the popup below the screen height so the WM never has to clip it;
    // the scroll area absorbs whatever the cards can't show. 70% leaves room
    // for the popup to sit near the selection without covering everything.
    const QScreen *scr = screen();
    const int cap = scr ? int(scr->availableGeometry().height() * 0.7) : 600;
    setMaximumHeight(cap);

    // QScrollArea's own sizeHint tops out around 24 font heights (and is
    // cached, so it can't be trusted mid-update either), which would leave
    // most of the cap unused for long results. Compute the fixed chrome
    // around the scroll viewport instead and let the scroll area claim its
    // real content height, bounded by the cap. Once the popup is laid out
    // the chrome is measured from live geometry — an estimate that is even
    // 2px short cascades into a pointless scrollbar; the hint-based estimate
    // only seeds the very first (pre-show) pass.
    int chrome;
    if (isVisible() && m_scroll->viewport()->height() > 0) {
        chrome = height() - m_scroll->viewport()->height();
    } else {
        const QMargins rm = m_container->layout()->contentsMargins();
        chrome = 2 * kShadowMargin + rm.top() + rm.bottom() + m_container->layout()->spacing()
            + m_titleRow->sizeHint().height() + 2; // +2: popupCard's stylesheet border
    }
    // The word-wrapping source label makes the content height-for-width: at
    // the real viewport width the height differs from the hint's width, which
    // would leave a few pixels of pointless scroll range on short results.
    // Two measures of the content height, and we need the larger one:
    // heightForWidth at the real viewport width (the wrapped source label is
    // height-for-width, so plain hints lie about it), and minimumSizeHint —
    // a resizable QScrollArea never sizes its widget below the latter, so a
    // viewport smaller than it gets a scrollbar no matter what hfw says.
    QLayout *contentLayout = m_scroll->widget()->layout();
    const int viewportWidth = m_scroll->viewport()->width();
    const int hfw = (contentLayout->hasHeightForWidth() && viewportWidth > 0)
        ? contentLayout->heightForWidth(viewportWidth)
        : m_scroll->widget()->sizeHint().height();
    const int content = qMax(hfw, m_scroll->widget()->minimumSizeHint().height());
    // +4px slack: at an exact content == viewport fit, ±1px layout rounding
    // can summon the scrollbar, which narrows the viewport, re-wraps the text
    // taller and thereby justifies itself. The slack breaks that feedback.
    const int inner = qMax(0, qMin(content + 4, cap - chrome));
    m_scroll->setMinimumHeight(inner);
    adjustSize(); // settles the width; it never shrinks an expanding window
    // …so pin the height explicitly, or a long result would leave the popup
    // tall forever once a shorter one follows. resize() alone doesn't work
    // here either: the layout re-applies a cached (stale) minimum size until
    // the pending LayoutRequest is processed, and that minimum wins over a
    // smaller resize() target. An explicit fixed height beats both. Width is
    // left as adjusted above: narrowing it would re-wrap the source label and
    // grow the content right back.
    setFixedHeight(chrome + inner);
}

void PopupWindow::scheduleAdjust()
{
    if (m_adjustPending)
        return;
    m_adjustPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_adjustPending = false;
        adjustPopupSize();
    });
}

QSize PopupWindow::sizeHint() const
{
    QSize hint = QWidget::sizeHint();
    hint.setHeight(qMin(hint.height(), maximumHeight()));
    return hint;
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
