#pragma once

#include "TranslateEngine.h"

#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

// Free, key-less Google translate engine (DESIGN.md §5.1).
//
// Uses the public translate_a/single endpoint (client=gtx) which returns both
// the sentence translation and, for single words, dictionary data (part of
// speech, glosses) and romanisation/phonetics. The endpoint host is configurable
// via Settings so users behind a restricted network can point at a mirror /
// self-hosted reverse proxy; only the host changes, the protocol is identical.
class GoogleWebEngine : public TranslateEngine {
    Q_OBJECT

public:
    explicit GoogleWebEngine(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QStringList configKeys() const override;

    void translate(const TranslateRequest &req) override;
    void cancel() override;

    // Parse a raw translate_a/single JSON body into a result. Exposed as a pure
    // static function so tests can exercise it against fixtures without network.
    // engineId/detectedSourceLang are filled from the payload; error is set when
    // the body cannot be parsed.
    static TranslateResult parseResponse(const QByteArray &body);

private:
    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
};
