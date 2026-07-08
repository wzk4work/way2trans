#pragma once

#include "engines/TranslateTypes.h"

#include <QHash>
#include <QWidget>

class QVBoxLayout;
class QLabel;
class QToolButton;
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
    void beginRequest(const TranslateRequest &request, const QStringList &engineIds);

    // Fill in (or replace) the card for result.engineId.
    void applyResult(const TranslateResult &result);

signals:
    // Emitted when the retry button on a failed card is pressed.
    void retryRequested(const QString &engineId);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void setPinned(bool pinned);
    void clearCards();

    QLabel *m_sourceLabel = nullptr;
    QLabel *m_langPairLabel = nullptr;
    QToolButton *m_pinButton = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    QHash<QString, EngineResultCard *> m_cards;
    bool m_pinned = false;
    bool m_singleWord = false;
};
