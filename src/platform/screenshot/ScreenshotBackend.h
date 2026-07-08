#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

// One captured screen: the pixels plus the geometry that places them in the
// virtual desktop coordinate space, and the device pixel ratio for HiDPI.
//
// A multi-monitor capture yields one ScreenImage per QScreen. geometry is in
// logical (device-independent) coordinates matching QScreen::geometry(); the
// QImage itself may be larger by `dpr` on HiDPI displays. The CaptureOverlay for
// a screen is positioned at geometry and scales the image down by dpr to draw a
// pixel-perfect frozen frame (DESIGN.md §3.2, §4.3, §7).
struct ScreenImage {
    QImage image; // captured pixels (physical resolution, dpr baked in)
    QRect geometry; // logical position/size in the virtual desktop
    qreal dpr = 1.0; // device pixel ratio of the source screen
};

// Grabs a full-screen snapshot of every screen (DESIGN.md §4.3).
//
// Asynchronous and signal-based like the other platform capabilities: a
// captureFullScreen() call returns immediately and the frames arrive on
// captured(), or captureFailed() on error. The platform specifics (X11
// QScreen::grabWindow vs. the xdg-desktop-portal Screenshot interface vs. the
// grim subprocess) live entirely behind this interface so ui/ and core/ never
// learn which session type is in play (AGENTS.md layering rule).
class ScreenshotBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~ScreenshotBackend() override = default;

    virtual void captureFullScreen() = 0;

    // Human-readable backend name for logs / the settings page.
    virtual QString name() const = 0;

signals:
    void captured(const QList<ScreenImage> &screens);
    void captureFailed(const QString &reason);
};
