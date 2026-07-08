#pragma once

#include <QString>

namespace w2t::ui {

// Shared stylesheet for engine result cards so the popup and the main window
// render results identically: a rounded body box (#resultBox) with a
// transparent inner text edit (#resultBody), an accent engine name, and compact
// card buttons. Kept in one place to avoid the two call sites drifting apart.
inline QString cardStyleSheet()
{
    return QStringLiteral(
        // The card is a transparent layout group; the body box below is the
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
        "  border-radius: 8px;"
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
