#pragma once

#include "PopupPlacement.h"

#include <QString>

// Fallback placement (DESIGN.md §4.4): centre the popup on the primary screen.
//
// Used when no cursor-aware backend is available. A later milestone can persist
// and restore the last position; M1 just centres.
class PlainPlacement : public PopupPlacement {
public:
    void place(QWindow *window, const QSize &hint) override;
    bool supportsCursorPlacement() const override { return false; }
    QString name() const override { return QStringLiteral("plain"); }
};
