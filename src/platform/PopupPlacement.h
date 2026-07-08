#pragma once

#include <QSize>

class QWindow;

// Positions the result popup on screen (DESIGN.md §4.4).
//
// Not a QObject: placement is a synchronous positioning strategy invoked right
// before the popup is shown. Platform differences (cursor-relative on X11,
// anchored on Wayland) hide behind this interface.
class PopupPlacement {
public:
    virtual ~PopupPlacement() = default;

    // Position window near the trigger point (or a configured anchor). hint is
    // the popup's expected size, used for off-screen clamping.
    virtual void place(QWindow *window, const QSize &hint) = 0;

    // Whether this backend can place the popup at the cursor. Wayland cannot.
    virtual bool supportsCursorPlacement() const = 0;

    // Name for logs / settings page.
    virtual QString name() const = 0;
};
