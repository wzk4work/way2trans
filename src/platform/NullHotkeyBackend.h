#pragma once

#include "HotkeyBackend.h"

// Fallback hotkey backend that registers nothing (DESIGN.md §4.2).
//
// Used when no real global-hotkey mechanism is available (e.g. xcb-keysyms was
// not compiled in, or a Wayland compositor without a shortcuts portal). It never
// grabs a key; the user is expected to bind `way2trans trigger <action>` in
// their desktop/compositor settings, which reaches the running instance over
// D-Bus (DESIGN.md §9). registerAction() returns false so callers show that
// guidance.
class NullHotkeyBackend : public HotkeyBackend {
    Q_OBJECT

public:
    using HotkeyBackend::HotkeyBackend;

    bool registerAction(const QString &actionId, const QKeySequence &sequence, const QString &description) override;
    bool canRebindAtRuntime() const override { return false; }
    QString name() const override { return QStringLiteral("null"); }
};
