#include "PlainPlacement.h"

#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QWindow>

void PlainPlacement::place(QWindow *window, const QSize &hint)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const QSize size = hint.isValid() && !hint.isEmpty() ? hint : window->size();
    const int x = avail.left() + (avail.width() - size.width()) / 2;
    const int y = avail.top() + (avail.height() - size.height()) / 2;
    window->setPosition(x, y);
}
