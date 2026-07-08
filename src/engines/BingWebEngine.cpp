#include "BingWebEngine.h"

#include "Logging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr auto kEngineId = "bing";
constexpr auto kHost = "www.bing.com";
constexpr auto kUserAgent = "Mozilla/5.0 (X11; Linux x86_64; rv:120.0) Gecko/20100101 Firefox/120.0";

// Bing uses its own language codes for a couple of tags; map the ones we emit.
QString toBingLang(const QString &bcp47)
{
    if (bcp47 == QLatin1String("zh") || bcp47 == QLatin1String("zh-CN"))
        return QStringLiteral("zh-Hans");
    if (bcp47 == QLatin1String("zh-TW"))
        return QStringLiteral("zh-Hant");
    if (bcp47.isEmpty())
        return QStringLiteral("auto-detect");
    return bcp47;
}
} // namespace

bool BingWebEngine::AuthToken::isExpired() const
{
    if (!isValid())
        return true;
    // key is the millisecond timestamp when the token was issued; expiryMs the
    // validity window. Refresh a little early to avoid races.
    const qint64 issued = key.toLongLong();
    if (issued <= 0 || expiryMs <= 0)
        return true;
    return QDateTime::currentMSecsSinceEpoch() > issued + expiryMs - 60'000;
}

BingWebEngine::BingWebEngine(QNetworkAccessManager *nam, QObject *parent)
    : TranslateEngine(parent)
    , m_nam(nam)
{ }

QString BingWebEngine::id() const
{
    return QString::fromLatin1(kEngineId);
}

QString BingWebEngine::displayName() const
{
    return QStringLiteral("Bing");
}

QStringList BingWebEngine::configKeys() const
{
    return {};
}

void BingWebEngine::translate(const TranslateRequest &req)
{
    cancel();
    if (m_token.isExpired()) {
        qCDebug(w2tEngines) << "bing token missing/expired; fetching";
        fetchToken(req);
    } else {
        sendTranslate(req);
    }
}

void BingWebEngine::cancel()
{
    if (m_reply) {
        m_reply->abort();
        m_reply.clear();
    }
}

void BingWebEngine::fetchToken(const TranslateRequest &pending)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://%1/translator").arg(kHost)));
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));

    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, pending]() {
        QNetworkReply *reply = m_reply;
        if (!reply)
            return;
        reply->deleteLater();
        m_reply.clear();

        if (reply->error() != QNetworkReply::NoError) {
            TranslateResult r;
            r.engineId = QString::fromLatin1(kEngineId);
            r.error = tr("Network error: %1").arg(reply->errorString());
            qCWarning(w2tEngines) << "bing token network error" << reply->errorString();
            emit resultReady(r);
            return;
        }

        m_token = parseToken(reply->readAll());
        if (!m_token.isValid()) {
            TranslateResult r;
            r.engineId = QString::fromLatin1(kEngineId);
            r.error = tr("Could not obtain Bing token (endpoint may have changed)");
            qCWarning(w2tEngines) << "bing token parse failed";
            emit resultReady(r);
            return;
        }
        qCDebug(w2tEngines) << "bing token acquired ig" << m_token.ig;
        sendTranslate(pending);
    });
}

void BingWebEngine::sendTranslate(const TranslateRequest &req)
{
    QUrl url(QStringLiteral("https://%1/ttranslatev3").arg(kHost));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("isVertical"), QStringLiteral("1"));
    urlQuery.addQueryItem(QStringLiteral("IG"), m_token.ig);
    urlQuery.addQueryItem(QStringLiteral("IID"), m_token.iid);
    url.setQuery(urlQuery);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Referer", QByteArrayLiteral("https://www.bing.com/translator"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("fromLang"), toBingLang(req.sourceLang));
    body.addQueryItem(QStringLiteral("to"), toBingLang(req.targetLang));
    body.addQueryItem(QStringLiteral("text"), req.text);
    body.addQueryItem(QStringLiteral("token"), m_token.token);
    body.addQueryItem(QStringLiteral("key"), m_token.key);

    qCDebug(w2tEngines) << "bing translate len" << req.text.size();
    m_reply = m_nam->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(m_reply, &QNetworkReply::finished, this, [this, req]() {
        QNetworkReply *reply = m_reply;
        if (!reply)
            return;
        reply->deleteLater();
        m_reply.clear();

        if (reply->error() != QNetworkReply::NoError) {
            // A stale token typically comes back as an HTTP error; drop it so the
            // next attempt refreshes.
            m_token = AuthToken {};
            TranslateResult r;
            r.engineId = QString::fromLatin1(kEngineId);
            r.error = tr("Network error: %1").arg(reply->errorString());
            qCWarning(w2tEngines) << "bing translate network error" << reply->errorString();
            emit resultReady(r);
            return;
        }

        emit resultReady(parseResponse(reply->readAll()));
    });
}

BingWebEngine::AuthToken BingWebEngine::parseToken(const QByteArray &html)
{
    AuthToken token;
    const QString text = QString::fromUtf8(html);

    static const QRegularExpression igRe(QStringLiteral("IG:\"([^\"]+)\""));
    static const QRegularExpression iidRe(QStringLiteral("data-iid=\"([^\"]+)\""));
    // params_AbusePreventionHelper = [1699999999999,"token-string",3600000];
    // (older pages used params_AbusePrevention; accept both.)
    static const QRegularExpression abuseRe(
        QStringLiteral("params_AbusePrevention(?:Helper)?\\s*=\\s*\\[([0-9]+),\"([^\"]+)\",([0-9]+)\\]"));

    if (const auto m = igRe.match(text); m.hasMatch())
        token.ig = m.captured(1);
    if (const auto m = iidRe.match(text); m.hasMatch())
        token.iid = m.captured(1) + QStringLiteral(".2"); // page appends a sequence index
    if (const auto m = abuseRe.match(text); m.hasMatch()) {
        token.key = m.captured(1);
        token.token = m.captured(2);
        token.expiryMs = m.captured(3).toLongLong();
    }
    return token;
}

TranslateResult BingWebEngine::parseResponse(const QByteArray &body)
{
    TranslateResult result;
    result.engineId = QString::fromLatin1(kEngineId);

    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        result.error = tr("Unexpected response (endpoint may have changed)");
        return result;
    }

    // ttranslatev3 returns an array: [{ detectedLanguage:{language},
    // translations:[{text,to}] }]. On error it returns an object with a
    // statusCode/errorMessage instead.
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        result.error = obj.value(QStringLiteral("errorMessage")).toString(tr("Bing translation failed"));
        return result;
    }
    if (!doc.isArray() || doc.array().isEmpty()) {
        result.error = tr("Empty response");
        return result;
    }

    const QJsonObject first = doc.array().at(0).toObject();
    result.detectedSourceLang
        = first.value(QStringLiteral("detectedLanguage")).toObject().value(QStringLiteral("language")).toString();
    const QJsonArray translations = first.value(QStringLiteral("translations")).toArray();
    if (!translations.isEmpty())
        result.translatedText = translations.at(0).toObject().value(QStringLiteral("text")).toString();

    if (result.translatedText.isEmpty())
        result.error = tr("No translation returned");

    return result;
}
