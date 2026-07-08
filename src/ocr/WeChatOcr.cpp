#include "WeChatOcr.h"

#include "Logging.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {
// A directory is a WeChat-OCR install if it carries all of these.
bool looksLikeWeChatOcrDir(const QString &dir)
{
    const QDir d(dir);
    return d.exists(QStringLiteral("wxocr")) && d.exists(QStringLiteral("libmmmojo.so"))
        && d.exists(QStringLiteral("ocr_model"));
}
} // namespace

WeChatOcr::WeChatOcr(QObject *parent)
    : OcrBackend(parent)
{
    m_installPath = probeInstallPath();
}

WeChatOcr::~WeChatOcr()
{
    if (m_helper) {
        m_helper->kill();
        m_helper->waitForFinished(500);
    }
    if (!m_pendingImagePath.isEmpty())
        QFile::remove(m_pendingImagePath);
}

QString WeChatOcr::id() const
{
    return QStringLiteral("wechat");
}

QString WeChatOcr::displayName() const
{
    return QStringLiteral("WeChat OCR");
}

QString WeChatOcr::probeInstallPath()
{
    // 1. Explicit user override in Settings ([ocr] wechatPath).
    const QString configured
        = Settings::instance().value(QStringLiteral("ocr/wechatPath")).toString();
    if (!configured.isEmpty() && looksLikeWeChatOcrDir(configured))
        return configured;

    // 2. Common native / packaged install roots. libmmmojo.so, wxocr and
    //    ocr_model sit at the WeChat root on native 4.x installs.
    QStringList roots;
    roots << QStringLiteral("/opt/wechat") << QStringLiteral("/opt/wechat/wechat")
          << QStringLiteral("/usr/lib/wechat") << QStringLiteral("/usr/share/wechat");
    const QString home = QDir::homePath();
    roots << home + QStringLiteral("/.local/share/wechat");
    // Flatpak: com.tencent.WeChat exposes its files under the app tree.
    roots << home + QStringLiteral("/.local/share/flatpak/app/com.tencent.WeChat/current/active/files")
          << QStringLiteral("/var/lib/flatpak/app/com.tencent.WeChat/current/active/files");

    for (const QString &root : std::as_const(roots)) {
        if (looksLikeWeChatOcrDir(root))
            return root;
    }
    return {};
}

bool WeChatOcr::isAvailable()
{
    return !probeInstallPath().isEmpty();
}

QString WeChatOcr::helperExecutablePath()
{
    // Prefer the helper installed next to the running binary (packaging places
    // way2trans and way2trans-ocr-helper in the same bindir).
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("way2trans-ocr-helper"));
    if (QFile::exists(beside))
        return beside;
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("way2trans-ocr-helper"));
    return onPath;
}

void WeChatOcr::failAndReset(const QString &reason)
{
    m_busy = false;
    if (!m_pendingImagePath.isEmpty()) {
        QFile::remove(m_pendingImagePath);
        m_pendingImagePath.clear();
    }
    emit failed(reason);
}

void WeChatOcr::startHelperIfNeeded()
{
    if (m_helper && m_helper->state() != QProcess::NotRunning)
        return;

    const QString helper = helperExecutablePath();
    if (helper.isEmpty()) {
        // Should not happen when isAvailable() gates construction, but guard.
        return;
    }

    m_helper = new QProcess(this);
    // Pass the discovered WeChat install path so the helper knows where the
    // proprietary components live (it never leaves the helper process).
    QStringList args;
    args << QStringLiteral("--wechat-path") << m_installPath;
    m_helper->setProgram(helper);
    m_helper->setArguments(args);

    connect(m_helper, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuffer += m_helper->readAllStandardOutput();
        // The protocol is one JSON object per line.
        int nl = -1;
        while ((nl = m_stdoutBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_stdoutBuffer.left(nl);
            m_stdoutBuffer.remove(0, nl + 1);
            if (line.trimmed().isEmpty())
                continue;

            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                qCWarning(w2tCore) << "wechat helper: bad JSON line:" << line.left(120);
                continue;
            }
            const QJsonObject obj = doc.object();
            const QString type = obj.value(QStringLiteral("type")).toString();
            if (type != QLatin1String("result"))
                continue; // ignore "ready"/log frames

            m_busy = false;
            if (!m_pendingImagePath.isEmpty()) {
                QFile::remove(m_pendingImagePath);
                m_pendingImagePath.clear();
            }

            if (!obj.value(QStringLiteral("error")).toString().isEmpty()) {
                emit failed(obj.value(QStringLiteral("error")).toString());
                continue;
            }

            QList<OcrLine> ocrLines;
            QString allText;
            const QJsonArray lines = obj.value(QStringLiteral("lines")).toArray();
            for (const QJsonValue &lv : lines) {
                const QJsonObject lo = lv.toObject();
                OcrLine ol;
                ol.text = lo.value(QStringLiteral("text")).toString();
                const QJsonArray box = lo.value(QStringLiteral("box")).toArray();
                if (box.size() == 4)
                    ol.box = QRect(box.at(0).toInt(), box.at(1).toInt(), box.at(2).toInt(), box.at(3).toInt());
                if (ol.text.isEmpty())
                    continue;
                ocrLines << ol;
                if (!allText.isEmpty())
                    allText += QLatin1Char('\n');
                allText += ol.text;
            }
            // The helper may also send a flat "text" field.
            if (allText.isEmpty())
                allText = obj.value(QStringLiteral("text")).toString();

            if (allText.trimmed().isEmpty())
                emit failed(tr("WeChat OCR returned no text"));
            else {
                emit recognizedLines(ocrLines);
                emit recognized(allText);
            }
        }
    });

    connect(m_helper, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        qCWarning(w2tCore) << "wechat helper process error:" << e;
        if (m_busy)
            failAndReset(tr("WeChat OCR helper failed; falling back"));
    });

    connect(m_helper, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        qCInfo(w2tCore) << "wechat helper exited code" << code << "status" << status;
        // A crash mid-request only fails that request; OcrService falls back to
        // the next backend (DESIGN.md §6.1). The helper is respawned on demand.
        if (m_busy)
            failAndReset(tr("WeChat OCR helper exited; falling back"));
        if (m_helper) {
            m_helper->deleteLater();
            m_helper = nullptr;
        }
    });

    qCInfo(w2tCore) << "starting wechat ocr helper:" << helper << "for install" << m_installPath;
    m_helper->start();
}

void WeChatOcr::recognize(const QImage &image, const QStringList &langHints)
{
    Q_UNUSED(langHints); // WeChat OCR auto-detects; hints are unused.

    if (m_installPath.isEmpty()) {
        emit failed(tr("WeChat OCR is not installed on this machine"));
        return;
    }
    if (helperExecutablePath().isEmpty()) {
        emit failed(tr("WeChat OCR helper (way2trans-ocr-helper) not found"));
        return;
    }
    if (m_busy) {
        emit failed(tr("WeChat OCR is busy with another request"));
        return;
    }
    if (image.isNull()) {
        emit failed(tr("No image to recognise"));
        return;
    }

    // Write the image to a temp PNG the helper reads by path (the protocol
    // passes a path, not pixels, DESIGN.md §6.1).
    const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QDir(tmpDir).filePath(
        QStringLiteral("way2trans-wechat-%1.png").arg(QCoreApplication::applicationPid()));
    if (!image.save(path, "PNG")) {
        emit failed(tr("Could not stage the image for WeChat OCR"));
        return;
    }
    m_pendingImagePath = path;
    m_busy = true;

    startHelperIfNeeded();
    if (!m_helper) {
        failAndReset(tr("Could not start the WeChat OCR helper"));
        return;
    }

    // Send the request as one JSON line: {"cmd":"recognize","image":"<path>"}.
    QJsonObject req;
    req.insert(QStringLiteral("cmd"), QStringLiteral("recognize"));
    req.insert(QStringLiteral("image"), path);
    const QByteArray payload = QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n';

    // If the helper is still starting, wait briefly for it (bounded).
    if (m_helper->state() == QProcess::Starting)
        m_helper->waitForStarted(2000);
    m_helper->write(payload);
}
