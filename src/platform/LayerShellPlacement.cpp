#include "LayerShellPlacement.h"

#include "Logging.h"
#include "Settings.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QMargins>
#include <QScreen>
#include <QWindow>

namespace {
// Gap from the anchored screen edge, in logical pixels.
constexpr int kEdgeMargin = 16;
} // namespace

LayerShellPlacement::LayerShellPlacement()
{
    // Shell::useLayerShell() must run *before* the popup QWindow is created, so
    // it is done in PlatformFactory::prepareWindowing() at startup rather than
    // here (this placement object is built after the popup). Kept as an explicit
    // no-op constructor to document that ordering dependency.
}

void LayerShellPlacement::place(QWindow *window, const QSize &hint)
{
    Q_UNUSED(hint);
    if (!window)
        return;

    LayerShellQt::Window *layer = LayerShellQt::Window::get(window);
    if (!layer) {
        // Shouldn't happen on Wayland once useLayerShell() ran before window
        // creation; log so a mis-ordered setup is diagnosable.
        qCWarning(w2tPlatform) << "LayerShellPlacement: no layer-shell handle for window"
                               << "(useLayerShell must run before the QWindow is created)";
        return;
    }

    // Overlay layer so the popup sits above normal windows; on-demand keyboard so
    // it can receive Esc to close without stealing focus permanently.
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
    layer->setScope(QStringLiteral("way2trans-popup"));

    // Anchor per [popup] anchor: default top-right corner; "center" leaves the
    // surface unanchored so the compositor centres it.
    const QString anchor
        = Settings::instance().value(QStringLiteral("popup/anchor"), QStringLiteral("top-right")).toString();
    if (anchor == QLatin1String("center")) {
        layer->setAnchors({});
        layer->setMargins(QMargins());
        qCInfo(w2tPlatform) << "LayerShellPlacement: anchor=center (overlay)";
    } else {
        layer->setAnchors(
            LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorRight));
        layer->setMargins(QMargins(0, kEdgeMargin, kEdgeMargin, 0));
        qCInfo(w2tPlatform) << "LayerShellPlacement: anchor=top-right (overlay)";
    }
}
