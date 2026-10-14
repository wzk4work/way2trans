#pragma once

#include <QString>

namespace w2t::ui {

// Shared stylesheet for engine result cards so the popup and the main window
// render results identically: one rounded surface (#resultBox) holding the
// header row (engine name + actions + collapse chevron) and a transparent
// inner text edit (#resultBody). Kept in one place to avoid the two call
// sites drifting apart.
inline QString cardStyleSheet()
{
    return QStringLiteral(
        // The card root is a transparent layout group; the box below is the
        // visible rounded surface, so results aren't double-boxed.
        "#engineCard { background: transparent; }"
        "#engineName {"
        "  font-weight: 600;"
        "  color: palette(highlight);"
        "  font-size: 12px;"
        "}"
        "#resultBox {"
        "  background: palette(base);"
        "  border: 1px solid palette(midlight);"
        "  border-radius: 10px;"
        "}"
        "#resultBody { background: transparent; border: none; }"
        "QToolButton#cardButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 5px;"
        "  padding: 1px 7px;"
        "  font-size: 11px;"
        "  color: palette(text);"
        "}"
        "QToolButton#cardButton:hover { background: palette(midlight); }");
}

} // namespace w2t::ui
