#pragma once

#include "SelectionSource.h"

// X11 selection reader (DESIGN.md §4.1).
//
// Reads the PRIMARY selection through Qt's clipboard in Selection mode
// (QClipboard::Selection). Qt returns it synchronously; we defer the signal to
// the event loop so callers always see the asynchronous contract regardless of
// backend.
class X11SelectionSource : public SelectionSource {
    Q_OBJECT

public:
    using SelectionSource::SelectionSource;

    void fetchSelection() override;
};
