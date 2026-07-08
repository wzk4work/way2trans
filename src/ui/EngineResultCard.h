#pragma once

#include "engines/TranslateTypes.h"

#include <QWidget>

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

signals:
    void retryRequested(const QString &engineId);

private:
    void setBody(const QString &plainText);
    void setDictBody(const DictEntry &dict, const QString &fallbackText);

    QString m_engineId;
    QLabel *m_header = nullptr;
    QTextEdit *m_body = nullptr;
    QToolButton *m_copyButton = nullptr;
    QToolButton *m_retryButton = nullptr;
    QVBoxLayout *m_layout = nullptr;
};
