#pragma once

#include "engines/TranslateTypes.h"

#include <QWidget>

class QFrame;
class QLabel;
class QTextEdit;
class QToolButton;
class QVBoxLayout;

// One engine's result card inside the popup (DESIGN.md §7).
//
// States: pending (skeleton spinner text), success (selectable result text with
// a copy button; dictionary layout for word lookups), and error (message + a
// retry button). The card owns its own copy-to-clipboard and emits retry so the
// popup can re-run just this engine.
class EngineResultCard : public QWidget {
    Q_OBJECT

public:
    EngineResultCard(const QString &engineId, const QString &displayName, QWidget *parent = nullptr);

    QString engineId() const { return m_engineId; }

    // Transition to the loading skeleton.
    void showPending();

    // Fill from a result: dictionary layout when preferDict and a DictEntry is
    // present, otherwise plain translated text; error layout when the result
    // carries an error.
    void showResult(const TranslateResult &result, bool preferDict);

    // Append a streaming delta (LLM). Switches the card out of the skeleton on
    // the first delta and grows the body as text arrives (DESIGN.md §5).
    void appendStreamDelta(const QString &delta);

signals:
    void retryRequested(const QString &engineId);

protected:
    // Recompute the body height when the card's width changes: only then is the
    // final wrap width known, so the height matches the actual number of lines.
    void resizeEvent(QResizeEvent *event) override;

private:
    void setBody(const QString &plainText);
    void setDictBody(const DictEntry &dict, const QString &fallbackText);
    void updateBodyHeight();

    QString m_engineId;
    QLabel *m_header = nullptr;
    QFrame *m_bodyFrame = nullptr;
    QTextEdit *m_body = nullptr;
    QToolButton *m_copyButton = nullptr;
    QToolButton *m_retryButton = nullptr;
    QVBoxLayout *m_layout = nullptr;
    bool m_pending = true; // showing the skeleton, not a real result
    bool m_streaming = false; // accumulating streamed deltas
    QString m_streamText; // text accumulated so far while streaming
    int m_maxBodyHeight = 260; // dictionary layouts get a taller cap
};
