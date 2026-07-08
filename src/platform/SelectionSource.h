#pragma once

#include <QObject>
#include <QString>

// Reads the current text selection from the desktop (DESIGN.md §4.1).
//
// Asynchronous, signal-based: fetchSelection() returns immediately and the
// result arrives on selectionFetched() or fetchFailed(). Platform specifics
// (X11 PRIMARY vs. Wayland data-control) live entirely behind this interface,
// so core/ and ui/ never learn which session type is in play (AGENTS.md).
class SelectionSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~SelectionSource() override = default;

    virtual void fetchSelection() = 0;

signals:
    void selectionFetched(const QString &text);
    void fetchFailed(const QString &reason);
};
