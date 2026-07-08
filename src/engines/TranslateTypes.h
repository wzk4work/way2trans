#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

// Shared value types for the translation-engine subsystem (DESIGN.md §5).
//
// These are plain data structs (no QObject) so they can be freely copied,
// queued across threads and stored in the memory cache.

// A single dictionary meaning under one part of speech.
struct DictMeaning {
    QString partOfSpeech; // "noun" / "verb" / "adj" ...
    QStringList definitions; // one or more glosses for this part of speech
};

// Dictionary-style result for the single-word lookup scenario (DESIGN.md §5.1).
struct DictEntry {
    QString word; // headword as understood by the engine
    QString phonetic; // IPA / pinyin / romanisation, may be empty
    QVector<DictMeaning> meanings; // grouped by part of speech

    bool isEmpty() const { return word.isEmpty() && phonetic.isEmpty() && meanings.isEmpty(); }
};

struct TranslateRequest {
    QString text;
    QString sourceLang; // "auto" or a BCP-47 tag such as "zh" / "en"
    QString targetLang; // BCP-47 tag, never "auto"
};

struct TranslateResult {
    QString engineId; // which engine produced this
    QString translatedText; // primary translation text
    QString detectedSourceLang; // engine-detected source language, may be empty
    std::optional<DictEntry> dict; // dictionary data for word lookups, if any
    QString error; // non-empty means the request failed

    bool ok() const { return error.isEmpty(); }
};
