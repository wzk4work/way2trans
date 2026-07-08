#pragma once

#include "ScreenshotBackend.h"

// X11 screenshot backend (DESIGN.md §4.3).
//
// Grabs each QScreen with QScreen::grabWindow(0) (the whole root window of that
// screen). Under xcb this is a synchronous X round-trip, but we hop through the
// event loop once so every backend honours the same async signal contract and a
// slow X server can't block the caller's stack.
class X11ScreenshotBackend : public ScreenshotBackend {
    Q_OBJECT

public:
    using ScreenshotBackend::ScreenshotBackend;

    void captureFullScreen() override;
    QString name() const override;

    // Pure helper (unit-tested): grab every screen currently known to Qt into a
    // ScreenImage list. Returns an empty list only when there are no screens.
    // Kept static so tests can call it without an event loop / signal plumbing.
    static QList<ScreenImage> grabAllScreens();
};
