#pragma once

#include <QObject>
#include <QString>

// Keeps the XDG autostart entry in sync with Settings::autostart()
// (DESIGN.md §10). The config value is the single source of truth: enabling
// writes ~/.config/autostart/io.github.way2trans.desktop with an Exec line
// that starts the daemon hidden (--hidden, DESIGN.md §9), disabling removes
// the file. Constructing the manager performs an initial sync so a stale Exec
// path (moved binary, relocated AppImage) heals on every launch.
//
// XDG autostart is a freedesktop convention shared by X11 and Wayland
// sessions alike, so this is core/ business logic, not a platform/ backend.
class AutostartManager : public QObject {
    Q_OBJECT

public:
    explicit AutostartManager(QObject *parent = nullptr);

    // ~/.config/autostart/io.github.way2trans.desktop (honours
    // XDG_CONFIG_HOME, which keeps tests hermetic).
    static QString entryFilePath();

    // The Exec command the entry launches: the current executable (or
    // $APPIMAGE when running from an AppImage) plus --hidden.
    static QString execLine();

    // Write or remove the entry to match `enabled`. Returns false when the
    // filesystem refused (directory not creatable, file not writable).
    bool sync(bool enabled);

private:
    static QString desktopEntryContents();
};
