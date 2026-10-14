#pragma once

#include <QObject>
#include <QString>

class Application;
class QSystemTrayIcon;
class QMenu;
class QAction;

// System tray entry (DESIGN.md §7 "TrayIcon").
//
// Qt routes QSystemTrayIcon through StatusNotifierItem / D-Bus on modern
// desktops (KDE/XFCE/Cinnamon). Every menu action forwards to the owning
// Application; M0 wiring only logs or pops the main window.
class TrayIcon : public QObject {
    Q_OBJECT

public:
    explicit TrayIcon(Application *app, QObject *parent = nullptr);
    ~TrayIcon() override;

    // True when a system tray is actually available in this session.
    bool isAvailable() const;
    void show();

    // Light "no selected text" hint (DESIGN.md §3.1: silent or gentle prompt).
    void notifyNoSelection();

    // Same, for the clipboard-translation entry when the clipboard is empty.
    void notifyClipboardEmpty();

    // Surface an OCR-unavailable / failure hint for screenshot translation
    // (DESIGN.md §3.2, §6): shown when no OCR backend is usable or all failed.
    void notifyOcrUnavailable(const QString &hint);

    // Silent capture OCR outcome (DESIGN.md §3.4): the only feedback the flow
    // gives, since it never opens a window.
    void notifyOcrCopied(const QString &preview);
    void notifyOcrEmpty();

private:
    Application *m_app;
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    QAction *m_pauseRecordingAction = nullptr; // checkable "pause recording"
};
