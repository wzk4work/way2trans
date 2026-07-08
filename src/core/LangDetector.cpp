#include "LangDetector.h"

#include <QChar>
#include <QHash>

namespace {
LangDetector::Script scriptForChar(char32_t cp)
{
    // Han ideographs (CJK Unified + common extensions we care about).
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x20000 && cp <= 0x2A6DF)
        || (cp >= 0xF900 && cp <= 0xFAFF))
        return LangDetector::Script::Han;
    // Japanese kana.
    if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x31F0 && cp <= 0x31FF))
        return LangDetector::Script::Kana;
    // Korean Hangul.
    if ((cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F))
        return LangDetector::Script::Hangul;
    // Cyrillic.
    if (cp >= 0x0400 && cp <= 0x04FF)
        return LangDetector::Script::Cyrillic;
    // Greek.
    if (cp >= 0x0370 && cp <= 0x03FF)
        return LangDetector::Script::Greek;
    // Arabic.
    if (cp >= 0x0600 && cp <= 0x06FF)
        return LangDetector::Script::Arabic;
    // Latin (basic + Latin-1 letters + Latin extended-A).
    if ((cp >= 0x0041 && cp <= 0x005A) || (cp >= 0x0061 && cp <= 0x007A) || (cp >= 0x00C0 && cp <= 0x024F))
        return LangDetector::Script::Latin;
    return LangDetector::Script::Unknown;
}
} // namespace

LangDetector::Script LangDetector::detectScript(const QString &text)
{
    QHash<Script, int> counts;
    // Iterate by UCS-4 so we correctly handle surrogate-pair CJK extensions.
    const QVector<uint> ucs4 = text.toUcs4();
    for (uint cp : ucs4) {
        const Script s = scriptForChar(cp);
        if (s != Script::Unknown)
            ++counts[s];
    }

    Script best = Script::Unknown;
    int bestCount = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    }
    return best;
}

QString LangDetector::detectLang(const QString &text)
{
    // Han takes precedence: mixed CJK text with a few Latin chars is still
    // Chinese for our target-language rule.
    const QVector<uint> ucs4 = text.toUcs4();
    int han = 0;
    int kana = 0;
    int hangul = 0;
    for (uint cp : ucs4) {
        switch (scriptForChar(cp)) {
        case Script::Han:
            ++han;
            break;
        case Script::Kana:
            ++kana;
            break;
        case Script::Hangul:
            ++hangul;
            break;
        default:
            break;
        }
    }
    // Kana present -> Japanese even if Han (kanji) also present.
    if (kana > 0)
        return QStringLiteral("ja");
    if (hangul > 0)
        return QStringLiteral("ko");
    if (han > 0)
        return QStringLiteral("zh");

    switch (detectScript(text)) {
    case Script::Latin:
        return QStringLiteral("en");
    case Script::Cyrillic:
        return QStringLiteral("ru");
    case Script::Greek:
        return QStringLiteral("el");
    case Script::Arabic:
        return QStringLiteral("ar");
    default:
        return QStringLiteral("auto");
    }
}
