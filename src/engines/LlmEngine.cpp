#include "LlmEngine.h"

#include "Logging.h"
#include "Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr auto kEngineId = "llm";

constexpr auto kDefaultEndpoint = "https://api.openai.com/v1";
// Placeholder model: the user is expected to set a real one in Settings.
constexpr auto kDefaultModel = "gpt-4o-mini";

// A source/target tag is nicer for the model spelled out; keep it simple and let
// the tag pass through, but turn "auto" into a neutral phrase.
QString describeSource(const QString &lang)
{
    return (lang.isEmpty() || lang == QLatin1String("auto")) ? QStringLiteral("the source language") : lang;
}
} // namespace

LlmEngine::LlmEngine(QNetworkAccessManager *nam, QObject *parent)
    : TranslateEngine(parent)
    , m_nam(nam)
{ }

QString LlmEngine::typeId() const
{
    return QString::fromLatin1(kEngineId);
}

QString LlmEngine::typeName() const
{
    return QStringLiteral("LLM");
}

QString LlmEngine::defaultPromptTemplate()
{
    // Kept intentionally terse: translate-only, no commentary, so the streamed
    // output is the translation itself.
    return QStringLiteral(
        "Translate the following text from {source} to {target}. Output only the translation, with no explanations.\n\n"
        "{text}");
}

QString LlmEngine::expandPrompt(const QString &tmpl, const TranslateRequest &req)
{
    QString out = tmpl;
    out.replace(QStringLiteral("{text}"), req.text);
    out.replace(QStringLiteral("{source}"), describeSource(req.sourceLang));
    out.replace(QStringLiteral("{target}"), req.targetLang);
    return out;
}

bool LlmEngine::parseStreamLine(const QByteArray &line, QString *deltaOut)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return false;
    // SSE frames we care about are "data: ..." lines; ignore comments/others.
    if (!trimmed.startsWith("data:"))
        return false;

    const QByteArray payload = trimmed.mid(5).trimmed();
    if (payload == "[DONE]")
        return true;

    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return false;
    const QJsonObject delta = choices.at(0).toObject().value(QStringLiteral("delta")).toObject();
    const QString content = delta.value(QStringLiteral("content")).toString();
    if (!content.isEmpty() && deltaOut)
        deltaOut->append(content);
    return false;
}

void LlmEngine::translate(const TranslateRequest &req)
{
    cancel();
    m_buffer.clear();
    m_accumulated.clear();
    m_done = false;

    Settings &settings = Settings::instance();
    const QString group = configGroup() + QLatin1Char('/');
    // No API key is a valid setup for local OpenAI-compatible servers (Ollama,
    // llama.cpp): send without an Authorization header and let the server
    // decide. Hosted providers will reject unauthenticated calls with a clear
    // error that surfaces on the result card.
    const QString apiKey = settings.value(group + QStringLiteral("apiKey")).toString().trimmed();

    QString endpoint = settings.value(group + QStringLiteral("endpoint"), QString::fromLatin1(kDefaultEndpoint))
                           .toString()
                           .trimmed();
    while (endpoint.endsWith(QLatin1Char('/')))
        endpoint.chop(1);
    const QString model
        = settings.value(group + QStringLiteral("model"), QString::fromLatin1(kDefaultModel)).toString().trimmed();
    const QString tmpl = settings.value(group + QStringLiteral("prompt"), defaultPromptTemplate()).toString();

    QNetworkRequest request(QUrl(endpoint + QStringLiteral("/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    request.setRawHeader("Accept", QByteArrayLiteral("text/event-stream"));

    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), expandPrompt(tmpl, req));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), model);
    payload.insert(QStringLiteral("stream"), true);
    payload.insert(QStringLiteral("messages"), QJsonArray {message});

    qCDebug(w2tEngines) << "llm request model" << model << "endpoint" << endpoint << "len" << req.text.size();
    m_reply = m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    // Stream the SSE body incrementally: parse each complete line as it lands and
    // emit deltas so the popup fills in as the model writes (DESIGN.md §5).
    connect(m_reply, &QNetworkReply::readyRead, this, &LlmEngine::consumeBuffer);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_reply;
        if (!reply)
            return;
        // Drain any bytes that arrived without a trailing newline.
        consumeBuffer();
        reply->deleteLater();
        m_reply.clear();

        TranslateResult r;
        r.engineId = id();
        if (reply->error() != QNetworkReply::NoError && !m_done) {
            r.error = tr("Network error: %1").arg(reply->errorString());
            qCWarning(w2tEngines) << "llm network error" << reply->errorString();
            emit resultReady(r);
            return;
        }
        if (m_accumulated.isEmpty()) {
            r.error = tr("No translation returned");
            emit resultReady(r);
            return;
        }
        r.translatedText = m_accumulated;
        emit resultReady(r);
    });
}

void LlmEngine::consumeBuffer()
{
    if (!m_reply || m_done)
        return;
    m_buffer += m_reply->readAll();

    // Pull out whole lines; leave any trailing partial line in the buffer.
    int nl = m_buffer.indexOf('\n');
    while (nl >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);

        QString delta;
        const bool done = parseStreamLine(line, &delta);
        if (!delta.isEmpty()) {
            m_accumulated += delta;
            emit partialResult(delta);
        }
        if (done) {
            m_done = true;
            return;
        }
        nl = m_buffer.indexOf('\n');
    }
}

void LlmEngine::cancel()
{
    if (m_reply) {
        m_reply->abort();
        m_reply.clear();
    }
    m_buffer.clear();
}
