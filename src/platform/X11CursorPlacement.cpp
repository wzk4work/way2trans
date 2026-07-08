#include "X11CursorPlacement.h"

#include "Logging.h"

#include <QCursor>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QWindow>

void X11CursorPlacement::place(QWindow *window, const QSize &hint)
{
    const QPoint cursor = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(cursor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    // Offset down-right of the cursor so it doesn't sit under the pointer.
    constexpr int kOffset = 12;
    QSize size = hint.isValid() && !hint.isEmpty() ? hint : window->size();
    int x = cursor.x() + kOffset;
    int y = cursor.y() + kOffset;

    // Clamp to the screen; if it would overflow right/bottom, flip to the other
    // side of the cursor.
    if (x + size.width() > avail.right())
        x = qMax(avail.left(), cursor.x() - kOffset - size.width());
    if (y + size.height() > avail.bottom())
        y = qMax(avail.top(), cursor.y() - kOffset - size.height());
    x = qBound(avail.left(), x, avail.right() - size.width());
    y = qBound(avail.top(), y, avail.bottom() - size.height());

    qCDebug(w2tPlatform) << "X11CursorPlacement at" << x << y << "cursor" << cursor;
    window->setPosition(x, y);
}
