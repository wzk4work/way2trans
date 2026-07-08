#include "X11ScreenshotBackend.h"

#include "Logging.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QTimer>

QString X11ScreenshotBackend::name() const
{
    return QStringLiteral("X11 (QScreen::grabWindow)");
}

QList<ScreenImage> X11ScreenshotBackend::grabAllScreens()
{
    QList<ScreenImage> shots;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (!screen)
            continue;
        // grabWindow(0) grabs the root window of this screen. The returned
        // QPixmap carries the screen's device pixel ratio, so image dimensions
        // are physical while geometry() stays logical.
        const QPixmap pixmap = screen->grabWindow(0);
        ScreenImage shot;
        shot.image = pixmap.toImage();
        shot.geometry = screen->geometry();
        shot.dpr = screen->devicePixelRatio();
        qCInfo(w2tPlatform) << "X11 screenshot: screen" << screen->name() << "geom" << shot.geometry << "img"
                            << shot.image.size() << "dpr" << shot.dpr;
        shots << shot;
    }
    return shots;
}

void X11ScreenshotBackend::captureFullScreen()
{
    // Hop through the event loop so the async signal contract holds even though
    // the underlying grab is synchronous under xcb.
    QTimer::singleShot(0, this, [this]() {
        const QList<ScreenImage> shots = grabAllScreens();
        if (shots.isEmpty()) {
            emit captureFailed(tr("No screens available to capture"));
            return;
        }
        emit captured(shots);
    });
}
