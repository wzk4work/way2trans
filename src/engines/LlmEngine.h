#pragma once

#include "TranslateEngine.h"

#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

// OpenAI-compatible Chat Completions translation engine (DESIGN.md §5).
//
// Talks to any OpenAI-compatible /v1/chat/completions endpoint (OpenAI, local
// llama.cpp/Ollama gateways, etc.). Configurable via Settings:
//   [engines/llm] endpoint (default https://api.openai.com/v1)
//                 apiKey
//                 model    (placeholder default; user must set a real model)
//                 prompt   (editable template with {text}/{source}/{target})
// Requests are streamed (stream=true): the SSE chunks are parsed as they arrive
// and emitted incrementally through partialResult(delta); resultReady fires once
// with the full text when the stream ends. Without an API key the engine fails
// fast with a hint and never touches the network (ships disabled by default,
// DESIGN.md §5.1).
class LlmEngine : public TranslateEngine {
    Q_OBJECT

public:
    explicit LlmEngine(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QStringList configKeys() const override;

    void translate(const TranslateRequest &req) override;
    void cancel() override;

    // Default prompt template used when none is configured. Exposed so the
    // settings UI can seed the editor with it.
    static QString defaultPromptTemplate();

    // Expand a prompt template, substituting {text}/{source}/{target}. Static and
    // pure so tests can verify placeholder handling without network.
    static QString expandPrompt(const QString &tmpl, const TranslateRequest &req);

    // Parse a single line off the SSE stream (a "data: {...}" line or the
    // terminal "data: [DONE]"). Appends any content delta to *deltaOut and
    // returns true when the line was the [DONE] sentinel. Static and pure so
    // tests can drive the streaming parser deterministically.
    static bool parseStreamLine(const QByteArray &line, QString *deltaOut);

private:
    void consumeBuffer(); // pull complete SSE lines out of m_buffer

    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_buffer; // accumulates partial SSE bytes across readyRead
    QString m_accumulated; // full text so far (for the final resultReady)
    bool m_done = false; // saw [DONE]; ignore any trailing bytes
};
