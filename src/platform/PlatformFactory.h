#pragma once

#include <QObject>
#include <QString>

#include <memory>

class SelectionSource;
class HotkeyBackend;
class PopupPlacement;

// Chooses the concrete platform backends for this session (DESIGN.md §2.1, §4).
//
// Selection is driven by QGuiApplication::platformName() and compile-time
// feature availability (e.g. whether the xcb-keysyms hotkey backend was built),
// with a Settings [platform] override for troubleshooting. Every decision is
// logged so the settings page / bug reports can show what was picked and why.
class PlatformFactory {
public:
    // One-time windowing setup that must run before any popup QWindow is created.
    // On Wayland with LayerShellQt this installs the wlr-layer-shell integration
    // so windows made afterwards become layer surfaces (LayerShellPlacement then
    // just configures anchors). No-op on X11 / when LayerShellQt is absent.
    static void prepareWindowing();

    // Create the selection source for this session. parent owns the result.
    static SelectionSource *createSelectionSource(QObject *parent);

    // Create the hotkey backend for this session. Falls back to the Null backend
    // when no real global-hotkey mechanism is available. parent owns the result.
    static HotkeyBackend *createHotkeyBackend(QObject *parent);

    // Create the popup placement strategy for this session.
    static std::unique_ptr<PopupPlacement> createPopupPlacement();

    // Log a one-line summary of the detected session (platform name, overrides).
    static void logProbeSummary();
};
