#pragma once

#include "PopupPlacement.h"

#include <QString>

// Cursor-relative popup placement for X11 (DESIGN.md §4.4).
//
// Positions the window a few pixels down-right of QCursor::pos(), then clamps it
// to the cursor's screen so it never spills off an edge.
class X11CursorPlacement : public PopupPlacement {
public:
    void place(QWindow *window, const QSize &hint) override;
    bool supportsCursorPlacement() const override { return true; }
    QString name() const override { return QStringLiteral("x11-cursor"); }
};
