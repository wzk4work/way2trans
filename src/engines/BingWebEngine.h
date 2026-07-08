#pragma once

#include "TranslateEngine.h"

#include <QDateTime>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

// Free, key-less Bing translate engine (DESIGN.md §5.1).
//
// The Bing web translator guards its ttranslatev3 endpoint with per-session
// tokens (IG, IID, and a signed key/token pair from the page's params_AbusePrevention
// blob). This engine first GETs the translator page to scrape those tokens,
// caches them, and reuses them until they expire, refreshing on demand. Bing is
// usually reachable without a proxy, so it is the default workhorse in networks
// where Google is blocked.
class BingWebEngine : public TranslateEngine {
    Q_OBJECT

public:
    explicit BingWebEngine(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QStringList configKeys() const override;

    void translate(const TranslateRequest &req) override;
    void cancel() override;

    // Tokens scraped from the translator landing page.
    struct AuthToken {
        QString ig;
        QString iid;
        QString key; // params_AbusePrevention[0] (unix-ms timestamp)
        QString token; // params_AbusePrevention[1]
        qint64 expiryMs = 0; // params_AbusePrevention[2], validity window in ms

        bool isValid() const { return !ig.isEmpty() && !token.isEmpty() && !key.isEmpty(); }
        bool isExpired() const;
    };

    // Extract auth tokens from a translator landing-page HTML body. Static and
    // pure so tests can exercise it against a fixture.
    static AuthToken parseToken(const QByteArray &html);

    // Parse a ttranslatev3 JSON body into a result. Static and pure for tests.
    static TranslateResult parseResponse(const QByteArray &body);

private:
    void fetchToken(const TranslateRequest &pending);
    void sendTranslate(const TranslateRequest &req);

    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    AuthToken m_token;
};
