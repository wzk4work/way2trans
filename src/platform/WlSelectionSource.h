#pragma once

#include "SelectionSource.h"

#include <QString>

class QProcess;

// Wayland selection reader (DESIGN.md §4.1).
//
// MVP mechanism: spawn `wl-paste --primary --no-newline` asynchronously via
// QProcess. wl-clipboard talks the wlr/ext data-control protocol internally, so
// it reads the PRIMARY selection without needing keyboard focus, which is what
// makes it usable for a global "translate selection" hotkey on Wayland.
//
// TODO(M6+, DESIGN.md §4.1, §13): replace the wl-paste subprocess with a native
// data-control client. Generate the protocol glue with wayland-scanner for
// ext-data-control-v1 (with a zwlr-data-control-v1 fallback), bind the manager
// from the wl_registry, create a data device on the default seat, and read the
// primary offer directly. That removes the runtime wl-clipboard dependency and
// the per-fetch process spawn. Kept out of M2 to keep the milestone small.
class WlSelectionSource : public SelectionSource {
    Q_OBJECT

public:
    explicit WlSelectionSource(QObject *parent = nullptr);

    void fetchSelection() override;

    // Whether the `wl-paste` executable is resolvable on PATH. PlatformFactory
    // probes this before selecting this backend; also used to give a precise
    // "install wl-clipboard" reason on fetchFailed().
    static bool isWlPasteAvailable();

    // Pure helper (unit-tested): decide what a wl-paste run means.
    // - exitOk false (non-zero exit / crash): treated as an EMPTY selection, not
    //   an error. wl-paste exits non-zero when there is simply no selection.
    // - otherwise the trimmed stdout is the selection text (may be empty).
    // Returns the text to emit via selectionFetched().
    static QString interpretOutput(const QByteArray &stdoutBytes, bool exitOk);

private:
    // Guard against a hung wl-paste (e.g. no compositor answering).
    static constexpr int kTimeoutMs = 2000;

    // At most one in-flight fetch; a new fetch while one is running is ignored.
    QProcess *m_process = nullptr;
};
