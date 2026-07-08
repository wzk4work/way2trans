#pragma once

#include "engines/TranslateTypes.h"

#include <QHash>
#include <QPoint>
#include <QWidget>

class QVBoxLayout;
class QFrame;
class QLabel;
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
    // pending card per engine id. Called before results start arriving.
    // When `editableSource` is true (screenshot / OCR flow, DESIGN.md §3.2) the
    // original text box becomes editable and committing an edit re-triggers the
    // translation via sourceEdited().
    void beginRequest(
        const TranslateRequest &request, const QStringList &engineIds, bool editableSource = false);

    // Fill in (or replace) the card for result.engineId.
    void applyResult(const TranslateResult &result);

signals:
    // Emitted when the retry button on a failed card is pressed.
    void retryRequested(const QString &engineId);

    // Emitted when the user edits the (editable) source text and commits it
    // (Ctrl+Enter or focus-out), asking for a re-translation of the new text.
    void sourceEdited(const QString &newText);

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

    QFrame *m_container = nullptr;
    QLabel *m_sourceLabel = nullptr;
    QPlainTextEdit *m_sourceEdit = nullptr; // editable source (OCR flow)
    QLabel *m_langPairLabel = nullptr;
    QToolButton *m_pinButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    QHash<QString, EngineResultCard *> m_cards;
    bool m_pinned = false;
    bool m_singleWord = false;
    bool m_everActivated = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
