#include "PlatformFactory.h"

#include "Logging.h"
#include "NullHotkeyBackend.h"
#include "PlainPlacement.h"
#include "PortalHotkeyBackend.h"
#include "Settings.h"
#include "WlSelectionSource.h"
#include "X11CursorPlacement.h"
#include "X11SelectionSource.h"

#ifdef WAY2TRANS_HAVE_XCB_KEYSYMS
#include "X11HotkeyBackend.h"
#endif

#ifdef WAY2TRANS_HAVE_LAYERSHELL
#include "LayerShellPlacement.h"
#include <LayerShellQt/Shell>
#endif

#include <QGuiApplication>

namespace {
bool isX11()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("xcb"));
}

bool isWayland()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

QString platformOverride(const QString &capability)
{
    return Settings::instance().value(QStringLiteral("platform/%1").arg(capability)).toString();
}
} // namespace

void PlatformFactory::prepareWindowing()
{
#ifdef WAY2TRANS_HAVE_LAYERSHELL
    // Only meaningful on a real Wayland session; on X11 it would needlessly set
    // the layer-shell env var. useLayerShell() must precede QWindow creation.
    const QString popupOverride = platformOverride(QStringLiteral("popup"));
    const bool layerWanted = popupOverride == QLatin1String("layer-shell") || (popupOverride.isEmpty() && isWayland());
    if (layerWanted) {
        qCInfo(w2tPlatform) << "prepareWindowing: enabling wlr-layer-shell integration";
        LayerShellQt::Shell::useLayerShell();
    }
#endif
}

void PlatformFactory::logProbeSummary()
{
    qCInfo(w2tPlatform) << "platform probe: QPA" << QGuiApplication::platformName()
#ifdef WAY2TRANS_HAVE_XCB_KEYSYMS
                        << "xcb-keysyms=yes"
#else
                        << "xcb-keysyms=no"
#endif
#ifdef WAY2TRANS_HAVE_LAYERSHELL
                        << "layer-shell=compiled"
#else
                        << "layer-shell=absent"
#endif
                        << "wl-paste=" << (WlSelectionSource::isWlPasteAvailable() ? "yes" : "no");
}

SelectionSource *PlatformFactory::createSelectionSource(QObject *parent)
{
    const QString override = platformOverride(QStringLiteral("selection"));

    // Explicit overrides win (troubleshooting, DESIGN.md §2.1 rule 5).
    if (override == QLatin1String("x11")) {
        qCInfo(w2tPlatform) << "selection backend: X11 (PRIMARY) [override]";
        return new X11SelectionSource(parent);
    }
    if (override == QLatin1String("wl-paste") || override == QLatin1String("wayland")) {
        qCInfo(w2tPlatform) << "selection backend: Wayland wl-paste [override]";
        return new WlSelectionSource(parent);
    }

    // Auto: Wayland prefers wl-paste when the tool is installed.
    if (override.isEmpty() && isWayland()) {
        if (WlSelectionSource::isWlPasteAvailable()) {
            qCInfo(w2tPlatform) << "selection backend: Wayland wl-paste";
            return new WlSelectionSource(parent);
        }
        // wl-paste missing: keep the Wayland backend so the user gets a precise
        // "install wl-clipboard" hint on fetch rather than a silent X11 no-op.
        qCWarning(w2tPlatform) << "selection backend: Wayland wl-paste (wl-clipboard not installed; "
                                  "fetch will prompt to install)";
        return new WlSelectionSource(parent);
    }

    if (override.isEmpty() && isX11()) {
        qCInfo(w2tPlatform) << "selection backend: X11 (PRIMARY)";
        return new X11SelectionSource(parent);
    }

    qCWarning(w2tPlatform) << "selection backend: no clear match (QPA" << QGuiApplication::platformName() << "override"
                           << override << ") - defaulting to X11 source";
    return new X11SelectionSource(parent);
}

HotkeyBackend *PlatformFactory::createHotkeyBackend(QObject *parent)
{
    const QString override = platformOverride(QStringLiteral("hotkey"));

    if (override == QLatin1String("null")) {
        qCInfo(w2tPlatform) << "hotkey backend: Null [override]";
        return new NullHotkeyBackend(parent);
    }

    // Portal GlobalShortcuts: preferred on Wayland (and honoured on explicit
    // override). Falls back to Null when the portal interface is not present.
    const bool wantPortal = override == QLatin1String("portal") || (override.isEmpty() && isWayland());
    if (wantPortal) {
        if (PortalHotkeyBackend::isPortalAvailable()) {
            qCInfo(w2tPlatform) << "hotkey backend: Portal GlobalShortcuts";
            return new PortalHotkeyBackend(parent);
        }
        qCInfo(w2tPlatform) << "hotkey backend: Portal requested but GlobalShortcuts portal unavailable; "
                               "using Null (bind `way2trans trigger` manually)";
        return new NullHotkeyBackend(parent);
    }

#ifdef WAY2TRANS_HAVE_XCB_KEYSYMS
    const bool wantX11 = override == QLatin1String("x11") || (override.isEmpty() && isX11());
    if (wantX11) {
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
        qCInfo(w2tPlatform) << "popup placement: plain [override]";
        return std::make_unique<PlainPlacement>();
    }
    if (override == QLatin1String("x11")) {
        qCInfo(w2tPlatform) << "popup placement: X11 cursor [override]";
        return std::make_unique<X11CursorPlacement>();
    }

    // layer-shell requires a real Wayland session (a layer surface is
    // meaningless off Wayland). Both the explicit override and the auto path are
    // gated on isWayland(); an override to layer-shell off Wayland warns and
    // falls through to the platform default rather than building a dead surface.
#ifdef WAY2TRANS_HAVE_LAYERSHELL
    const bool wantLayer = override == QLatin1String("layer-shell") || override.isEmpty();
    if (wantLayer && isWayland()) {
        qCInfo(w2tPlatform) << "popup placement: layer-shell (overlay)";
        return std::make_unique<LayerShellPlacement>();
    }
    if (override == QLatin1String("layer-shell") && !isWayland())
        qCWarning(w2tPlatform) << "popup override layer-shell requested but session is not Wayland; ignoring";
#else
    if (override == QLatin1String("layer-shell"))
        qCWarning(w2tPlatform) << "popup override layer-shell requested but LayerShellQt not compiled in";
    if (override.isEmpty() && isWayland())
        qCWarning(w2tPlatform) << "Wayland session but LayerShellQt not compiled in; using plain placement";
#endif

    if (override.isEmpty() && isX11()) {
        qCInfo(w2tPlatform) << "popup placement: X11 cursor";
        return std::make_unique<X11CursorPlacement>();
    }

    qCInfo(w2tPlatform) << "popup placement: plain (fallback)";
    return std::make_unique<PlainPlacement>();
}
