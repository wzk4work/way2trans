#include "GoogleWebEngine.h"

#include "Logging.h"
#include "Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr auto kEngineId = "google";
constexpr auto kDefaultHost = "translate.googleapis.com";
// Settings key allowing the host to be swapped for a mirror / reverse proxy.
constexpr auto kKeyHost = "engines/google/host";

// The translate_a/single body is a JSON array whose slots vary by requested dt
// flags. Layout we rely on:
//   [0] -> array of [translatedChunk, sourceChunk, ...] sentence pieces
//          plus a trailing [null,null,null, srcTranslit] romanisation entry
//   [1] -> dictionary: array of [pos, [glosses...], ...] per part of speech
//   [2] -> detected source language code
QString joinSentences(const QJsonArray &sentences, QString &srcTranslitOut)
{
    QString translated;
    for (const QJsonValue &v : sentences) {
        const QJsonArray chunk = v.toArray();
        if (chunk.isEmpty())
            continue;
        // A romanisation-only trailing entry has a null [0]; its [3] carries the
        // source transliteration (phonetics for e.g. Chinese pinyin).
        if (chunk.at(0).isNull()) {
            if (chunk.size() > 3 && chunk.at(3).isString())
                srcTranslitOut = chunk.at(3).toString();
            continue;
        }
        translated += chunk.at(0).toString();
    }
    return translated;
}

DictEntry parseDict(const QJsonArray &dictArray)
{
    DictEntry entry;
    for (const QJsonValue &v : dictArray) {
        const QJsonArray posBlock = v.toArray();
        if (posBlock.size() < 2)
            continue;
        DictMeaning meaning;
        meaning.partOfSpeech = posBlock.at(0).toString();
        for (const QJsonValue &g : posBlock.at(1).toArray()) {
            const QString gloss = g.toString();
            if (!gloss.isEmpty())
                meaning.definitions << gloss;
        }
        if (!meaning.definitions.isEmpty())
            entry.meanings << meaning;
    }
    return entry;
}
} // namespace

GoogleWebEngine::GoogleWebEngine(QNetworkAccessManager *nam, QObject *parent)
    : TranslateEngine(parent)
    , m_nam(nam)
{ }

QString GoogleWebEngine::id() const
{
    return QString::fromLatin1(kEngineId);
}

QString GoogleWebEngine::displayName() const
{
    return QStringLiteral("Google");
}

QStringList GoogleWebEngine::configKeys() const
{
    return {QString::fromLatin1(kKeyHost)};
}

void GoogleWebEngine::translate(const TranslateRequest &req)
{
    cancel();

    const QString host
        = Settings::instance().value(QString::fromLatin1(kKeyHost), QString::fromLatin1(kDefaultHost)).toString();

    QUrl url(QStringLiteral("https://%1/translate_a/single").arg(host));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client"), QStringLiteral("gtx"));
    query.addQueryItem(QStringLiteral("sl"), req.sourceLang.isEmpty() ? QStringLiteral("auto") : req.sourceLang);
    query.addQueryItem(QStringLiteral("tl"), req.targetLang);
    // dt=t sentence, dt=bd dictionary, dt=rm romanisation/phonetics.
    query.addQueryItem(QStringLiteral("dt"), QStringLiteral("t"));
    query.addQueryItem(QStringLiteral("dt"), QStringLiteral("bd"));
    query.addQueryItem(QStringLiteral("dt"), QStringLiteral("rm"));
    query.addQueryItem(QStringLiteral("q"), req.text);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) way2trans"));

    qCDebug(w2tEngines) << "google request host" << host << "len" << req.text.size();
    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_reply;
        if (!reply)
            return;
        reply->deleteLater();
        m_reply.clear();

        if (reply->error() != QNetworkReply::NoError) {
            TranslateResult r;
            r.engineId = QString::fromLatin1(kEngineId);
            r.error = tr("Network error: %1").arg(reply->errorString());
            qCWarning(w2tEngines) << "google network error" << reply->errorString();
            emit resultReady(r);
            return;
        }

        emit resultReady(parseResponse(reply->readAll()));
    });
}

void GoogleWebEngine::cancel()
{
    if (m_reply) {
        m_reply->abort();
        m_reply.clear();
    }
}

TranslateResult GoogleWebEngine::parseResponse(const QByteArray &body)
{
    TranslateResult result;
    result.engineId = QString::fromLatin1(kEngineId);

    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        result.error = tr("Unexpected response (endpoint may have changed)");
        return result;
    }

    const QJsonArray root = doc.array();
    if (root.isEmpty()) {
        result.error = tr("Empty response");
        return result;
    }

    QString srcTranslit;
    result.translatedText = joinSentences(root.at(0).toArray(), srcTranslit);

    if (root.size() > 2 && root.at(2).isString())
        result.detectedSourceLang = root.at(2).toString();

    // Dictionary data (single-word lookups): root[1].
    if (root.size() > 1 && root.at(1).isArray()) {
        DictEntry entry = parseDict(root.at(1).toArray());
        entry.phonetic = srcTranslit;
        if (!entry.isEmpty())
            result.dict = entry;
    }

    if (result.translatedText.isEmpty() && !result.dict.has_value())
        result.error = tr("No translation returned");

    return result;
}
