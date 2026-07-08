#pragma once

#include "ScreenshotBackend.h"

#include <QString>

class QDBusPendingCallWatcher;

// Wayland screenshot backend via xdg-desktop-portal (DESIGN.md §4.3).
//
// Calls org.freedesktop.portal.Screenshot.Screenshot with interactive=false so
// the portal captures the whole desktop without its own selection UI (we draw
// our own selection overlay for a consistent cross-platform experience,
// DESIGN.md §3.2). The portal returns a Request object path; the actual result
// (a file:// URI to a PNG) arrives asynchronously on that request's Response
// signal. We load the PNG, then immediately delete the temporary file the
// portal wrote.
//
// Compositor notes: KDE prompts once for permission and can remember it. The
// portal returns a single composited image spanning the whole logical desktop,
// not per-screen frames, so we synthesise one ScreenImage covering the virtual
// geometry (dpr from the primary screen). That is sufficient for the overlay,
// which slices the frame per screen by geometry.
class PortalScreenshotBackend : public ScreenshotBackend {
    Q_OBJECT

public:
    explicit PortalScreenshotBackend(QObject *parent = nullptr);

    void captureFullScreen() override;
    QString name() const override;

    // Whether the org.freedesktop.portal.Screenshot interface is present on the
    // session bus. PlatformFactory probes this before choosing this backend.
    static bool isPortalAvailable();

private slots:
    // Response signal from the portal Request object. results["uri"] holds the
    // file:// path to the captured PNG. Declared as a slot so the string-based
    // QDBusConnection::connect (matched by name at runtime) can reach it.
    void onPortalResponse(uint response, const QVariantMap &results);

private:
    // Turn the file:// URI the portal returned into a loaded ScreenImage list,
    // deleting the temporary file afterwards. Emits captured/captureFailed.
    void handleResponseUri(const QString &uri);

    QString m_requestPath;
};
