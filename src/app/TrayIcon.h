#pragma once

#include <QObject>
#include <QString>

class Application;
class QSystemTrayIcon;
class QMenu;

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

    // Surface an OCR-unavailable / failure hint for screenshot translation
    // (DESIGN.md §3.2, §6): shown when no OCR backend is usable or all failed.
    void notifyOcrUnavailable(const QString &hint);

private:
    Application *m_app;
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
};
