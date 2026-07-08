#include "PlatformFactory.h"

#include "Logging.h"
#include "NullHotkeyBackend.h"
#include "PlainPlacement.h"
#include "Settings.h"
#include "X11CursorPlacement.h"
#include "X11SelectionSource.h"

#ifdef WAY2TRANS_HAVE_XCB_KEYSYMS
#include "X11HotkeyBackend.h"
#endif

#include <QGuiApplication>

namespace {
bool isX11()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("xcb"));
}

QString platformOverride(const QString &capability)
{
    return Settings::instance().value(QStringLiteral("platform/%1").arg(capability)).toString();
}
} // namespace

void PlatformFactory::logProbeSummary()
{
    qCInfo(w2tPlatform) << "platform probe: QPA" << QGuiApplication::platformName()
#ifdef WAY2TRANS_HAVE_XCB_KEYSYMS
                        << "xcb-keysyms=yes";
#else
                        << "xcb-keysyms=no";
#endif
}

SelectionSource *PlatformFactory::createSelectionSource(QObject *parent)
{
    const QString override = platformOverride(QStringLiteral("selection"));
    if (override == QLatin1String("x11") || (override.isEmpty() && isX11())) {
        qCInfo(w2tPlatform) << "selection backend: X11 (PRIMARY)";
        return new X11SelectionSource(parent);
    }
    // No Wayland backend in M1; default to X11 which is a no-op off X11 but keeps
    // the contract. Wayland arrives in M2.
    qCWarning(w2tPlatform) << "selection backend: no match for override" << override << "- using X11 source";
    return new X11SelectionSource(parent);
}

HotkeyBackend *PlatformFactory::createHotkeyBackend(QObject *parent)
{
    const QString override = platformOverride(QStringLiteral("hotkey"));

#ifdef WAY2TRANS_HAVE_XCB_KEYSYMS
    const bool wantX11 = override == QLatin1String("x11") || (override.isEmpty() && isX11());
    if (wantX11 && override != QLatin1String("null")) {
        qCInfo(w2tPlatform) << "hotkey backend: X11 XGrabKey";
        return new X11HotkeyBackend(parent);
    }
#else
    if (override == QLatin1String("x11"))
        qCWarning(w2tPlatform) << "hotkey override x11 requested but xcb-keysyms not compiled in";
#endif

    qCInfo(w2tPlatform) << "hotkey backend: Null (bind `way2trans trigger` manually)";
    return new NullHotkeyBackend(parent);
}

std::unique_ptr<PopupPlacement> PlatformFactory::createPopupPlacement()
{
    const QString override = platformOverride(QStringLiteral("popup"));
    if (override == QLatin1String("plain")) {
        qCInfo(w2tPlatform) << "popup placement: plain (override)";
        return std::make_unique<PlainPlacement>();
    }
    if (override == QLatin1String("x11") || (override.isEmpty() && isX11())) {
        qCInfo(w2tPlatform) << "popup placement: X11 cursor";
        return std::make_unique<X11CursorPlacement>();
    }
    qCInfo(w2tPlatform) << "popup placement: plain (fallback)";
    return std::make_unique<PlainPlacement>();
}
