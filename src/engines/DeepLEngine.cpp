#include "DeepLEngine.h"

#include "Logging.h"
#include "Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr auto kEngineId = "deepl";

constexpr auto kFreeHost = "api-free.deepl.com";
constexpr auto kProHost = "api.deepl.com";
} // namespace

DeepLEngine::DeepLEngine(QNetworkAccessManager *nam, QObject *parent)
    : TranslateEngine(parent)
    , m_nam(nam)
{ }

QString DeepLEngine::typeId() const
{
    return QString::fromLatin1(kEngineId);
}

QString DeepLEngine::typeName() const
{
    return QStringLiteral("DeepL");
}

QString DeepLEngine::hostForKey(const QString &apiKey)
{
    // DeepL Free keys carry a ":fx" suffix and must hit the free host.
    return apiKey.endsWith(QLatin1String(":fx")) ? QString::fromLatin1(kFreeHost) : QString::fromLatin1(kProHost);
}

QString DeepLEngine::toDeepLLang(const QString &bcp47, bool isTarget)
{
    const QString lower = bcp47.toLower();
    if (lower.isEmpty() || lower == QLatin1String("auto"))
        return {}; // auto-detect (source only; caller must not pass this as target)

    // DeepL uses upper-case codes; it distinguishes EN-GB/EN-US and PT-BR/PT-PT
    // as targets, and now accepts ZH-HANS/ZH-HANT. Keep the common cases explicit
    // and upper-case the rest.
    if (lower.startsWith(QLatin1String("zh"))) {
        if (lower == QLatin1String("zh-tw") || lower == QLatin1String("zh-hant"))
            return isTarget ? QStringLiteral("ZH-HANT") : QStringLiteral("ZH");
        return isTarget ? QStringLiteral("ZH-HANS") : QStringLiteral("ZH");
    }
    if (lower == QLatin1String("en"))
        return isTarget ? QStringLiteral("EN-US") : QStringLiteral("EN");
    if (lower == QLatin1String("pt"))
        return isTarget ? QStringLiteral("PT-PT") : QStringLiteral("PT");

    // Strip any region for the general case (DeepL wants the base code apart from
    // the special-cased targets above).
    return bcp47.left(2).toUpper();
}

void DeepLEngine::translate(const TranslateRequest &req)
{
    cancel();

    const QString apiKey = Settings::instance().value(configGroup() + QStringLiteral("/apiKey")).toString().trimmed();
    if (apiKey.isEmpty()) {
        // No key configured: fail fast with a hint, never touch the network.
        TranslateResult r;
        r.engineId = id();
        r.error = tr("Set a DeepL API key in Settings to use this engine");
        qCInfo(w2tEngines) << "deepl: no API key configured; skipping network";
        QMetaObject::invokeMethod(this, [this, r]() { emit resultReady(r); }, Qt::QueuedConnection);
        return;
    }

    QUrl url(QStringLiteral("https://%1/v2/translate").arg(hostForKey(apiKey)));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Authorization", QByteArrayLiteral("DeepL-Auth-Key ") + apiKey.toUtf8());

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("text"), req.text);
    body.addQueryItem(QStringLiteral("target_lang"), toDeepLLang(req.targetLang, /*isTarget=*/true));
    const QString source = toDeepLLang(req.sourceLang, /*isTarget=*/false);
    if (!source.isEmpty())
        body.addQueryItem(QStringLiteral("source_lang"), source);

    qCDebug(w2tEngines) << "deepl request len" << req.text.size() << "host" << hostForKey(apiKey);
    m_reply = m_nam->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_reply;
        if (!reply)
            return;
        reply->deleteLater();
        m_reply.clear();

        if (reply->error() != QNetworkReply::NoError) {
            TranslateResult r;
            r.engineId = id();
            // Surface the API's own error body when present (DeepL returns a JSON
            // {"message": ...} for 4xx), otherwise the transport error.
            const TranslateResult parsed = parseResponse(reply->readAll());
            r.error = parsed.ok() ? tr("Network error: %1").arg(reply->errorString()) : parsed.error;
            qCWarning(w2tEngines) << "deepl network error" << reply->errorString();
            emit resultReady(r);
            return;
        }

        TranslateResult parsedResult = parseResponse(reply->readAll());
        parsedResult.engineId = id();
        emit resultReady(parsedResult);
    });
}

void DeepLEngine::cancel()
{
    if (m_reply) {
        m_reply->abort();
        m_reply.clear();
    }
}

TranslateResult DeepLEngine::parseResponse(const QByteArray &body)
{
    TranslateResult result; // engineId is stamped by the caller (static context)

    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = tr("Unexpected response (endpoint may have changed)");
        return result;
    }

    const QJsonObject obj = doc.object();
    // Error bodies carry a "message" field (e.g. auth failure, quota exceeded).
    if (obj.contains(QStringLiteral("message"))) {
        result.error = obj.value(QStringLiteral("message")).toString(tr("DeepL translation failed"));
        return result;
    }

    const QJsonArray translations = obj.value(QStringLiteral("translations")).toArray();
    if (translations.isEmpty()) {
        result.error = tr("Empty response");
        return result;
    }

    const QJsonObject first = translations.at(0).toObject();
    result.translatedText = first.value(QStringLiteral("text")).toString();
    result.detectedSourceLang = first.value(QStringLiteral("detected_source_language")).toString().toLower();

    if (result.translatedText.isEmpty())
        result.error = tr("No translation returned");

    return result;
}
