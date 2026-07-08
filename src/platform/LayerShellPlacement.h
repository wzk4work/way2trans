#pragma once

#include "PopupPlacement.h"

#include <QString>

// Wayland popup placement via LayerShellQt (DESIGN.md §4.4).
//
// Wraps the popup's QWindow in a wlr-layer-shell overlay surface anchored to a
// configured screen corner (default: top-right; [popup] anchor=center centres
// it). Wayland gives no global cursor coordinates, so cursor-relative placement
// is impossible here — supportsCursorPlacement() returns false, which is a
// platform limitation, not a defect (DESIGN.md §4.4).
//
// Compiled only when WAY2TRANS_HAVE_LAYERSHELL is set (LayerShellQt present).
// PlatformFactory only instantiates it on a real Wayland session; otherwise it
// falls back to PlainPlacement.
class LayerShellPlacement : public PopupPlacement {
public:
    LayerShellPlacement();

    void place(QWindow *window, const QSize &hint) override;
    bool supportsCursorPlacement() const override { return false; }
    QString name() const override { return QStringLiteral("layer-shell"); }
};
