#include "TextCleaner.h"

#include <QChar>
#include <QRegularExpression>
#include <QStringList>

namespace {
// True if a line's last non-space char is sentence-terminating punctuation, so
// the following newline is a real paragraph break, not a hard wrap.
bool endsWithTerminator(const QString &line)
{
    for (int i = line.size() - 1; i >= 0; --i) {
        const QChar c = line.at(i);
        if (c.isSpace())
            continue;
        // ASCII + CJK full-width terminators plus common trailing punctuation.
        static const QString kTerminators = QStringLiteral(".!?:;,。！？；，”’)]}");
        return kTerminators.contains(c);
    }
    return true; // empty line: treat as a break
}

// True if the next line begins (after leading space) with a lowercase letter,
// the tell-tale of a mid-sentence hard wrap.
bool startsLower(const QString &line)
{
    for (const QChar c : line) {
        if (c.isSpace())
            continue;
        return c.isLetter() && c.isLower();
    }
    return false;
}
} // namespace

QString TextCleaner::clean(const QString &text, bool mergeHardWraps)
{
    QString normalised = text;
    normalised.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalised.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    if (!mergeHardWraps) {
        // Collapse runs of spaces/tabs but keep line structure, then trim.
        static const QRegularExpression spaces(QStringLiteral("[ \\t]+"));
        normalised.replace(spaces, QStringLiteral(" "));
        static const QRegularExpression trailingSpace(QStringLiteral(" *\\n"));
        normalised.replace(trailingSpace, QStringLiteral("\n"));
        return normalised.trimmed();
    }

    const QStringList lines = normalised.split(QLatin1Char('\n'));
    QString out;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i);
        out += line;
        if (i == lines.size() - 1)
            break;

        const QString &next = lines.at(i + 1);
        const bool hardWrap = !next.trimmed().isEmpty() && !endsWithTerminator(line) && startsLower(next);
        out += hardWrap ? QLatin1Char(' ') : QLatin1Char('\n');
    }

    // Collapse any redundant spaces produced by joins and trim edges.
    static const QRegularExpression spaces(QStringLiteral("[ \\t]+"));
    out.replace(spaces, QStringLiteral(" "));
    static const QRegularExpression blankLines(QStringLiteral("\\n{3,}"));
    out.replace(blankLines, QStringLiteral("\n\n"));
    static const QRegularExpression spaceAroundNewline(QStringLiteral(" *\\n *"));
    out.replace(spaceAroundNewline, QStringLiteral("\n"));
    return out.trimmed();
}
