#include "PortalScreenshotBackend.h"

#include "Logging.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QUrl>
#include <QVariantMap>

namespace {
constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kScreenshotIface = "org.freedesktop.portal.Screenshot";
constexpr auto kRequestIface = "org.freedesktop.portal.Request";
} // namespace

PortalScreenshotBackend::PortalScreenshotBackend(QObject *parent)
    : ScreenshotBackend(parent)
{
}

QString PortalScreenshotBackend::name() const
{
    return QStringLiteral("Portal (org.freedesktop.portal.Screenshot)");
}

bool PortalScreenshotBackend::isPortalAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    // Probe the interface's version property; a valid reply means the portal is
    // present and exposes Screenshot.
    QDBusInterface iface(QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
        QString::fromLatin1(kScreenshotIface), bus);
    if (!iface.isValid())
        return false;
    const QVariant version = iface.property("version");
    return version.isValid();
}

void PortalScreenshotBackend::captureFullScreen()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit captureFailed(tr("Session bus not available for the screenshot portal"));
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
        QString::fromLatin1(kPortalPath), QString::fromLatin1(kScreenshotIface), QStringLiteral("Screenshot"));
    // parent_window is empty (no X11/Wayland handle); options carry the flags.
    QVariantMap options;
    // Non-interactive: no portal selection UI, we draw our own overlay
    // (DESIGN.md §3.2). Some portals still show a permission prompt on first use.
    options.insert(QStringLiteral("interactive"), false);
    call << QString() << options;

    QDBusPendingCall pending = bus.asyncCall(call);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, bus](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            emit captureFailed(tr("Screenshot portal call failed: %1").arg(reply.error().message()));
            return;
        }
        m_requestPath = reply.value().path();
        qCInfo(w2tPlatform) << "portal screenshot request at" << m_requestPath;

        // The result arrives on the Request object's Response signal.
        QDBusConnection sessionBus = bus;
        const bool ok = sessionBus.connect(QString::fromLatin1(kPortalService), m_requestPath,
            QString::fromLatin1(kRequestIface), QStringLiteral("Response"), this,
            SLOT(onPortalResponse(uint, QVariantMap)));
        if (!ok)
            emit captureFailed(tr("Could not subscribe to the screenshot portal response"));
    });
}

void PortalScreenshotBackend::onPortalResponse(uint response, const QVariantMap &results)
{
    // response: 0 = success, 1 = user cancelled, 2 = ended some other way.
    if (response != 0) {
        emit captureFailed(tr("Screenshot portal request was not fulfilled (code %1)").arg(response));
        return;
    }
    const QString uri = results.value(QStringLiteral("uri")).toString();
    if (uri.isEmpty()) {
        emit captureFailed(tr("Screenshot portal returned no image location"));
        return;
    }
    handleResponseUri(uri);
}

// Note: SLOT()/SIGNAL() string-based connect is unavoidable here because the
// portal Response signal is not described by a compiled Qt meta-object we own;
// the D-Bus signal is matched by name/signature at runtime. This is the same
// pattern PortalHotkeyBackend uses (DESIGN.md §4.2). The handler below is
// declared as a slot in the header.

void PortalScreenshotBackend::handleResponseUri(const QString &uri)
{
    const QString localPath = QUrl(uri).toLocalFile();
    if (localPath.isEmpty()) {
        emit captureFailed(tr("Screenshot portal returned an unreadable location"));
        return;
    }

    QImage image(localPath);
    // Delete the temporary file the portal wrote as soon as we have the pixels
    // in memory (DESIGN.md §4.3): the portal owns the file, we only borrow it.
    QFile::remove(localPath);

    if (image.isNull()) {
        emit captureFailed(tr("Could not load the screenshot the portal produced"));
        return;
    }

    // The portal hands back a single composited image spanning the whole logical
    // desktop. Represent it as one ScreenImage covering the virtual geometry.
    ScreenImage shot;
    shot.image = image;
    QRect virtualGeom;
    for (const QScreen *screen : QGuiApplication::screens())
        virtualGeom = virtualGeom.united(screen->geometry());
    shot.geometry = virtualGeom.isNull() ? QRect(QPoint(0, 0), image.size()) : virtualGeom;
    const QScreen *primary = QGuiApplication::primaryScreen();
    shot.dpr = primary ? primary->devicePixelRatio() : 1.0;
    image.setDevicePixelRatio(shot.dpr);
    shot.image = image;

    qCInfo(w2tPlatform) << "portal screenshot loaded" << image.size() << "geom" << shot.geometry;
    emit captured(QList<ScreenImage> {shot});
}
