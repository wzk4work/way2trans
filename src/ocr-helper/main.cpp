// way2trans-ocr-helper : isolated helper process that drives the proprietary
// WeChat OCR engine on behalf of the main way2trans process (DESIGN.md §6.1).
//
// AGENTS.md private-binary rules, enforced here by construction:
//   1. Nothing from WeChat is bundled or linked into way2trans; this helper
//      loads the user's local components at RUNTIME via dlopen (see below).
//   2. The proprietary code runs ONLY in this separate process; if it crashes
//      or misbehaves it takes down the helper, not the main app. The main
//      process talks to us solely over a line-delimited stdio JSON protocol and
//      falls back to Tesseract on any failure.
//
// Protocol (one JSON object per line, both directions):
//   in : {"cmd":"recognize","image":"/path/to/frame.png"}
//   out: {"type":"result","lines":[{"text":"...","box":[x,y,w,h]}, ...]}
//        {"type":"result","error":"reason"}            (on failure)
//        {"type":"ready"}                              (once, at startup)
//
// STATUS: the protocol, process framework, crash isolation and probe/absence
// handling are fully working. The actual mojo IPC bridge to WeChat's wxocr
// component is a documented TODO (see recognizeWithWeChat): it needs
// libmmmojo.so's mojo bootstrap + the wxocr subprocess handshake, which cannot
// be implemented/verified without a running WeChat OCR service to test against.
// Until that lands the helper returns a structured "not yet implemented" error
// so the main process degrades cleanly to Tesseract.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTextStream>

#include <cstdio>
#include <unistd.h>

namespace {

// Emit one JSON object as a single line on stdout and flush immediately so the
// parent's line-buffered reader sees it without waiting on a full pipe buffer.
void writeLine(const QJsonObject &obj)
{
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
    ::fwrite(payload.constData(), 1, static_cast<size_t>(payload.size()), stdout);
    ::fflush(stdout);
}

QJsonObject resultError(const QString &reason)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("result"));
    o.insert(QStringLiteral("error"), reason);
    return o;
}

// ---------------------------------------------------------------------------
// WeChat OCR bridge.
//
// TODO(WeChat OCR IPC, DESIGN.md §6.1): implement the real recognise here.
//
// The path validated against real installs (e.g. GitHub swigger/wechat-ocr and
// its bindings) is roughly:
//   1. dlopen(<wechatPath>/libmmmojo.so) and resolve the mmmojo bootstrap
//      exports (MMMojoEnvInit / CreateMMMojo* / the "point" callback table).
//   2. Launch the <wechatPath>/wxocr executable as a mojo child, passing the
//      ocr_model directory and a mojo message-pipe handle bootstrapped through
//      libmmmojo (the component speaks Chromium mojo, not a plain socket).
//   3. Send an OCR request referencing the PNG path; receive a protobuf/mojo
//      reply carrying recognised lines with per-line quads.
//   4. Convert quads to axis-aligned boxes and stream them back as our JSON.
//
// This is deliberately NOT stubbed with a fake success: WeChat is not verifiable
// on this machine's setup without risking loading unvetted binaries, and a false
// positive would mask the Tesseract fallback. We return a structured error so
// the main process degrades to Tesseract (its designed behaviour). The dlopen
// bootstrap, model-dir wiring and mojo handshake are the remaining work.
QJsonObject recognizeWithWeChat(const QString &wechatPath, const QString &imagePath)
{
    if (wechatPath.isEmpty())
        return resultError(QStringLiteral("WeChat OCR path not provided to helper"));
    if (!QFile::exists(imagePath))
        return resultError(QStringLiteral("Image not found: ") + imagePath);

    const QString ocrBin = QDir(wechatPath).filePath(QStringLiteral("wxocr"));
    const QString mojoLib = QDir(wechatPath).filePath(QStringLiteral("libmmmojo.so"));
    const QString modelDir = QDir(wechatPath).filePath(QStringLiteral("ocr_model"));
    if (!QFile::exists(ocrBin) || !QFile::exists(mojoLib) || !QDir(modelDir).exists())
        return resultError(QStringLiteral("Incomplete WeChat OCR install at ") + wechatPath);

    // TODO: real mojo bootstrap + wxocr handshake here (see block comment).
    return resultError(QStringLiteral("WeChat OCR IPC bridge not yet implemented (helper framework ready); "
                                      "falling back to Tesseract"));
}

void handleCommand(const QByteArray &line, const QString &wechatPath)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        writeLine(resultError(QStringLiteral("Malformed request JSON")));
        return;
    }
    const QJsonObject obj = doc.object();
    const QString cmd = obj.value(QStringLiteral("cmd")).toString();
    if (cmd == QLatin1String("recognize")) {
        const QString imagePath = obj.value(QStringLiteral("image")).toString();
        writeLine(recognizeWithWeChat(wechatPath, imagePath));
    } else if (cmd == QLatin1String("ping")) {
        QJsonObject pong;
        pong.insert(QStringLiteral("type"), QStringLiteral("pong"));
        writeLine(pong);
    } else {
        writeLine(resultError(QStringLiteral("Unknown command: ") + cmd));
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("way2trans-ocr-helper"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("way2trans WeChat OCR helper (isolated process; stdio JSON protocol)"));
    parser.addHelpOption();
    QCommandLineOption wechatPathOption(
        QStringList {QStringLiteral("wechat-path")}, QStringLiteral("WeChat install directory"), QStringLiteral("dir"));
    parser.addOption(wechatPathOption);
    parser.process(app);
    const QString wechatPath = parser.value(wechatPathOption);

    // Announce readiness so the parent can start sending requests.
    {
        QJsonObject ready;
        ready.insert(QStringLiteral("type"), QStringLiteral("ready"));
        writeLine(ready);
    }

    // Read line-delimited commands from stdin without blocking the event loop.
    auto *notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
    auto *buffer = new QByteArray;
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [notifier, buffer, wechatPath]() {
        char chunk[4096];
        const ssize_t n = ::read(STDIN_FILENO, chunk, sizeof(chunk));
        if (n <= 0) {
            // EOF: parent closed the pipe; exit cleanly.
            QCoreApplication::quit();
            return;
        }
        buffer->append(chunk, static_cast<int>(n));
        int nl = -1;
        while ((nl = buffer->indexOf('\n')) >= 0) {
            const QByteArray cmdLine = buffer->left(nl);
            buffer->remove(0, nl + 1);
            if (!cmdLine.trimmed().isEmpty())
                handleCommand(cmdLine, wechatPath);
        }
        Q_UNUSED(notifier);
    });

    return app.exec();
}
