#include "X11SelectionSource.h"

#include "Logging.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QTimer>

void X11SelectionSource::fetchSelection()
{
    // QClipboard::Selection maps to the X11 PRIMARY selection. The read is
    // synchronous under xcb, but we hop through the event loop so every backend
    // honours the same async signal contract (and so a slow X round-trip can't
    // block the caller's stack).
    QTimer::singleShot(0, this, [this]() {
        const QClipboard *clipboard = QGuiApplication::clipboard();
        if (!clipboard || !clipboard->supportsSelection()) {
            qCWarning(w2tPlatform) << "PRIMARY selection unsupported on this platform";
            emit fetchFailed(tr("Selection clipboard unavailable"));
            return;
        }
        const QString text = clipboard->text(QClipboard::Selection);
        qCDebug(w2tPlatform) << "X11 PRIMARY selection length" << text.size();
        emit selectionFetched(text);
    });
}
