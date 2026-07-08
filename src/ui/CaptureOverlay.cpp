#include "CaptureOverlay.h"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

namespace {
// Dimming applied outside the selection (and over the whole frame before a
// selection starts). Inside the selection the frozen frame shows at full
// brightness so the user sees exactly what will be captured.
constexpr int kDimAlpha = 120;
constexpr int kHandleSize = 3;
} // namespace

CaptureOverlay::CaptureOverlay(const QImage &frame, const QRect &screenGeometry, QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint)
    , m_frame(frame)
    , m_screenGeometry(screenGeometry)
{
    setObjectName(QStringLiteral("captureOverlay"));
    // The overlay owns the whole screen while it is up.
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    // Place at the target screen geometry (logical coords).
    setGeometry(screenGeometry);
}

QRect CaptureOverlay::selectionRect() const
{
    return QRect(m_origin, m_current).normalized();
}

void CaptureOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    // 1. The frozen frame as the background. m_frame carries the source dpr, so
    //    drawing it into the widget rect yields a pixel-perfect full-screen copy.
    p.drawImage(rect(), m_frame);

    // 2. Dimming mask over everything.
    p.fillRect(rect(), QColor(0, 0, 0, kDimAlpha));

    const QRect sel = selectionRect();
    if (m_selecting && sel.width() > 0 && sel.height() > 0) {
        // 3. Punch the selection back to full brightness by re-drawing that
        //    slice of the frame over the mask.
        const qreal sx = static_cast<qreal>(m_frame.width()) / width();
        const qreal sy = static_cast<qreal>(m_frame.height()) / height();
        const QRect srcRect(QPoint(qRound(sel.x() * sx), qRound(sel.y() * sy)),
            QSize(qRound(sel.width() * sx), qRound(sel.height() * sy)));
        p.drawImage(sel, m_frame, srcRect);

        // 4. Selection border.
        QPen pen(QColor(64, 160, 255), 1.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(sel);

        // Corner handles for a Flameshot-like feel.
        p.setBrush(QColor(64, 160, 255));
        p.setPen(Qt::NoPen);
        for (const QPoint &corner : {sel.topLeft(), sel.topRight(), sel.bottomLeft(), sel.bottomRight()})
            p.drawRect(QRect(corner - QPoint(kHandleSize, kHandleSize),
                QSize(kHandleSize * 2 + 1, kHandleSize * 2 + 1)));

        // 5. Live size readout above the selection (or below if near the top).
        const QString label = QStringLiteral("%1 x %2").arg(sel.width()).arg(sel.height());
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() + 1);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int tw = fm.horizontalAdvance(label) + 12;
        const int th = fm.height() + 6;
        int lx = sel.x();
        int ly = sel.y() - th - 4;
        if (ly < 0)
            ly = sel.y() + 4;
        const QRect badge(lx, ly, tw, th);
        p.setBrush(QColor(0, 0, 0, 180));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(badge, 4, 4);
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, label);
    } else {
        // Before dragging: a hint centred on the screen.
        p.setPen(QColor(255, 255, 255, 200));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() + 2);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, tr("Drag to select an area to translate — Esc to cancel"));
    }
}

void CaptureOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_selecting = true;
    m_origin = event->pos();
    m_current = event->pos();
    update();
}

void CaptureOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting) {
        m_current = event->pos();
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_selecting || m_finished) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_selecting = false;
    const QRect sel = selectionRect();

    // Too-small selections (a stray click) count as a cancel, not a 0px OCR.
    if (sel.width() < 4 || sel.height() < 4) {
        m_finished = true;
        emit cancelled();
        close();
        return;
    }

    // Crop the frozen frame in image-pixel space (account for scaling between
    // the logical widget size and the frame's physical resolution).
    const qreal sx = static_cast<qreal>(m_frame.width()) / width();
    const qreal sy = static_cast<qreal>(m_frame.height()) / height();
    const QRect srcRect(QPoint(qRound(sel.x() * sx), qRound(sel.y() * sy)),
        QSize(qRound(sel.width() * sx), qRound(sel.height() * sy)));
    const QImage cropped = m_frame.copy(srcRect);

    // Report the selection rectangle in this screen's coordinate space so the
    // caller can place the result popup next to it (DESIGN.md §4.4 exception).
    const QRect screenRect(m_screenGeometry.topLeft() + sel.topLeft(), sel.size());

    m_finished = true;
    emit regionSelected(cropped, screenRect);
    close();
}

void CaptureOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (!m_finished) {
            m_finished = true;
            emit cancelled();
        }
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}
