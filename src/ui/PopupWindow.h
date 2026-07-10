#pragma once

#include "engines/TranslateTypes.h"

#include <QHash>
#include <QPoint>
#include <QWidget>

class QHBoxLayout;
class QVBoxLayout;
class QFrame;
class QLabel;
class QScrollArea;
class QToolButton;
class QPlainTextEdit;
class EngineResultCard;

// Frameless rounded result popup (DESIGN.md §7).
//
// Shows the source text (collapsible) plus the detected language pair at the
// top, and one card per engine below. The popup is shown in a skeleton state
// immediately and each card fills in as its engine result arrives. Esc or focus
// loss closes it, unless it has been pinned (the 📌 button). For single-word
// lookups a card prefers the engine's DictEntry (phonetic / part of speech /
// definitions) over the plain sentence translation.
class PopupWindow : public QWidget {
    Q_OBJECT

public:
    explicit PopupWindow(QWidget *parent = nullptr);

    // Reset to a fresh skeleton for a new request: show the source text and one
    // pending card per engine id. `engineNames` maps engine (service instance)
    // ids to their user-visible names; missing ids fall back to the raw id.
    // When `editableSource` is true (screenshot / OCR flow, DESIGN.md §3.2) the
    // original text box becomes editable and committing an edit re-triggers the
    // translation via sourceEdited().
    void beginRequest(const TranslateRequest &request, const QStringList &engineIds,
        const QHash<QString, QString> &engineNames, bool editableSource = false);

    // Fill in (or replace) the card for result.engineId.
    void applyResult(const TranslateResult &result);

    // Append a streaming delta to the given engine's card (LLM, DESIGN.md §5).
    void applyPartial(const QString &engineId, const QString &delta);

    // Reflect whether the current lookup word is saved in the vocabulary, so the
    // star renders filled (★) or hollow (☆). Called by the app after a lookup.
    void setSaved(bool saved);

    // The layout's hint can exceed the screen-height cap (the scroll area
    // absorbs the difference), so clamp it: placement backends size the window
    // from this hint and must see the capped height.
    QSize sizeHint() const override;

signals:
    // Emitted when the retry button on a failed card is pressed.
    void retryRequested(const QString &engineId);

    // Emitted when the user edits the (editable) source text and commits it
    // (Ctrl+Enter or focus-out), asking for a re-translation of the new text.
    void sourceEdited(const QString &newText);

    // Emitted when the star is toggled on: save the current word to the
    // vocabulary. `context` is the full source text (the sentence for a
    // whole-sentence selection, DESIGN.md §8.3).
    void saveRequested(const QString &word, const QString &context);
    // Emitted when the star is toggled off: remove the current word.
    void unsaveRequested(const QString &word);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    // Drag-to-move: the window is frameless, so the header/background acts as a
    // title bar. Events reach us either directly (container background) or by
    // propagation from non-interactive children; interactive widgets (text,
    // buttons) keep their own mouse handling.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setPinned(bool pinned);
    void clearCards();
    void onStarToggled(bool checked);
    // adjustSize() with the height capped to a fraction of the current screen,
    // so long results scroll inside the popup instead of overflowing off-screen.
    void adjustPopupSize();
    // Deferred, coalesced adjustPopupSize(): card body resizes invalidate
    // layouts via posted LayoutRequest events, so size hints read in the same
    // call stack are stale — adjust one event-loop iteration later instead.
    void scheduleAdjust();

    QFrame *m_container = nullptr;
    QFrame *m_sourceCard = nullptr; // rounded surface around the editable source
    QScrollArea *m_scroll = nullptr; // scrollable source + cards region
    QHBoxLayout *m_titleRow = nullptr; // fixed chrome above the scroll area
    QLabel *m_sourceLabel = nullptr;
    QPlainTextEdit *m_sourceEdit = nullptr; // editable source (OCR flow)
    QLabel *m_langPairLabel = nullptr;
    QToolButton *m_starButton = nullptr; // vocabulary ☆/★ (word lookups)
    QToolButton *m_pinButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    QHash<QString, EngineResultCard *> m_cards;
    bool m_pinned = false;
    bool m_singleWord = false;
    bool m_adjustPending = false; // a deferred scheduleAdjust() is queued
    bool m_everActivated = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
    QString m_currentWord; // the queried word for the star (single-word lookups)
    QString m_currentContext; // full source text saved as vocab context
};
