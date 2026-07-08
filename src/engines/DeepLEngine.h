#pragma once

#include "TranslateEngine.h"

#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

// DeepL official API engine (DESIGN.md §5).
//
// Uses DeepL's REST v2 /translate endpoint. The API key is read from Settings
// ([engines/deepl] apiKey); a Free-tier key (suffix ":fx") targets
// api-free.deepl.com, a Pro key targets api.deepl.com. Auth is header-based
// (Authorization: DeepL-Auth-Key <key>). Without a key the engine fails fast
// with a localized hint and issues no network request, so it degrades gracefully
// when unconfigured (the engine ships disabled by default, DESIGN.md §5.1).
class DeepLEngine : public TranslateEngine {
    Q_OBJECT

public:
    explicit DeepLEngine(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QStringList configKeys() const override;

    void translate(const TranslateRequest &req) override;
    void cancel() override;

    // Pick the API host for a key: Free-tier keys end in ":fx" and use
    // api-free.deepl.com, everything else uses api.deepl.com. Static and pure so
    // tests can exercise the routing without network.
    static QString hostForKey(const QString &apiKey);

    // Map a BCP-47 tag to a DeepL source/target language code (upper-cased,
    // region kept only where DeepL distinguishes it, e.g. EN-US / ZH). Empty in
    // means "auto" (source only). Static and pure for tests.
    static QString toDeepLLang(const QString &bcp47, bool isTarget);

    // Parse a /translate JSON body into a result. Static and pure for tests.
    static TranslateResult parseResponse(const QByteArray &body);

private:
    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
};
