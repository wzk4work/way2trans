#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

// Full-screen selection overlay for screenshot translation (DESIGN.md §3.2, §7).
//
// Shows a frozen frame of a captured screen (the screenshot painted as the
// background) under a dimming mask, and lets the user drag out a rectangle
// (Flameshot-style) with a live size readout. On mouse release it crops the
// selected region out of the frozen frame and emits regionSelected(); Esc emits
// cancelled().
//
// One overlay is created per screen (multi-monitor). Each overlay is a
// borderless full-screen window positioned at its screen's geometry. On X11 the
// window is a frameless top-level; on Wayland the caller arranges for it to be a
// layer-shell overlay surface (PlatformFactory::prepareWindowing already
// installs the integration). The selection region reported back is in the
// overlay's screen coordinate space, which the caller uses to place the result
// popup next to the selection.
class CaptureOverlay : public QWidget {
    Q_OBJECT

public:
    // `frame` is the frozen screenshot for this screen (already scaled to the
    // logical geometry / carries its dpr). `screenGeometry` is the screen's
    // position+size in the virtual desktop; the overlay is shown there.
    CaptureOverlay(const QImage &frame, const QRect &screenGeometry, QWidget *parent = nullptr);

signals:
    // Emitted once when the user completes a selection. `cropped` is the pixels
    // inside the selection; `screenRect` is the selection rectangle in this
    // screen's coordinate space (origin = screenGeometry.topLeft()).
    void regionSelected(const QImage &cropped, const QRect &screenRect);

    // Emitted when the user presses Esc (or the selection is abandoned).
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    // The current selection rectangle in local widget coordinates (normalised).
    QRect selectionRect() const;

    QImage m_frame; // frozen screenshot for this screen
    QRect m_screenGeometry; // this screen's geometry in the virtual desktop
    QPoint m_origin; // drag start (local coords)
    QPoint m_current; // drag current (local coords)
    bool m_selecting = false;
    bool m_finished = false; // guards double-emit of regionSelected/cancelled
};
