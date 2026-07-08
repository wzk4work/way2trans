#pragma once

#include <QString>

// Normalises copied text before translation (DESIGN.md §3.1).
//
// PDFs and terminals wrap lines hard: a paragraph gets a literal newline at each
// visual line break. Sending those to a translator fragments sentences. This
// cleaner rejoins hard-wrapped lines (line ends without terminal punctuation and
// the next line starts lowercase -> same sentence, join with a space) and
// collapses redundant whitespace. The behaviour can be disabled from Settings.
class TextCleaner {
public:
    // Clean the given text. When mergeHardWraps is false only whitespace is
    // trimmed/collapsed and newlines are preserved.
    static QString clean(const QString &text, bool mergeHardWraps = true);
};
