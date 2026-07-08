#include "GrimScreenshotBackend.h"

#include "Logging.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryFile>

GrimScreenshotBackend::GrimScreenshotBackend(QObject *parent)
    : ScreenshotBackend(parent)
{
}

QString GrimScreenshotBackend::name() const
{
    return QStringLiteral("grim (wlroots)");
}

bool GrimScreenshotBackend::isGrimAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("grim")).isEmpty();
}

void GrimScreenshotBackend::captureFullScreen()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        qCDebug(w2tPlatform) << "grim capture already in flight; ignoring";
        return;
    }

    // Temporary output path in the runtime dir; grim writes a PNG there.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_outputPath = QDir(dir).filePath(QStringLiteral("way2trans-grim-%1.png").arg(QCoreApplication::applicationPid()));

    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        m_process->deleteLater();
        m_process = nullptr;

        if (status != QProcess::NormalExit || exitCode != 0) {
            QFile::remove(m_outputPath);
            emit captureFailed(tr("grim exited with an error (is a wlroots compositor running?)"));
            return;
        }

        QImage image(m_outputPath);
        QFile::remove(m_outputPath);
        if (image.isNull()) {
            emit captureFailed(tr("Could not load the image grim produced"));
            return;
        }

        // grim emits a single image of the whole output layout. Represent it as
        // one ScreenImage covering the virtual desktop geometry.
        ScreenImage shot;
        QRect virtualGeom;
        for (const QScreen *screen : QGuiApplication::screens())
            virtualGeom = virtualGeom.united(screen->geometry());
        shot.geometry = virtualGeom.isNull() ? QRect(QPoint(0, 0), image.size()) : virtualGeom;
        const QScreen *primary = QGuiApplication::primaryScreen();
        shot.dpr = primary ? primary->devicePixelRatio() : 1.0;
        image.setDevicePixelRatio(shot.dpr);
        shot.image = image;
        qCInfo(w2tPlatform) << "grim screenshot loaded" << image.size() << "geom" << shot.geometry;
        emit captured(QList<ScreenImage> {shot});
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit captureFailed(tr("Could not run grim. Install grim to capture on wlroots."));
    });

    qCInfo(w2tPlatform) << "running grim ->" << m_outputPath;
    m_process->start(QStringLiteral("grim"), {m_outputPath});
    if (!m_process->waitForStarted(kTimeoutMs)) {
        // errorOccurred handles emitting captureFailed.
        qCWarning(w2tPlatform) << "grim failed to start within" << kTimeoutMs << "ms";
    }
}
