#pragma once

#include <QString>

// Writing-system heuristic language detector (DESIGN.md §5.1).
//
// One-period scope is only to tell the "Chinese vs. foreign" case apart well
// enough to choose a target language; fine-grained Latin-script disambiguation
// is left to each engine's own auto-detect. Detection is a pure function over a
// sample of the text's code points, so it is cheap and side-effect free.
class LangDetector {
public:
    // Coarse writing-system buckets we distinguish.
    enum class Script {
        Unknown,
        Han, // CJK ideographs (treated as Chinese for target-lang rules)
        Latin, // Latin alphabet
        Cyrillic,
        Greek,
        Kana, // Japanese hiragana/katakana
        Hangul, // Korean
        Arabic,
    };

    // Detect the dominant script of the sample.
    static Script detectScript(const QString &text);

    // Detect a coarse BCP-47 language tag ("zh" / "en" / "ru" / "ja" ...).
    // Returns "auto" when the text is too short or ambiguous.
    static QString detectLang(const QString &text);
};
