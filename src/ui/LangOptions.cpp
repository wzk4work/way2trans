#include "LangOptions.h"

#include <QComboBox>
#include <QCoreApplication>

namespace {
// tr() context for the language names (lupdate groups by this name).
struct LangOptions {
    Q_DECLARE_TR_FUNCTIONS(LangOptions)
};

struct Option {
    const char *rule;
    QString name;
};

QList<Option> options()
{
    // Curated targets every built-in engine understands. Keep the list short:
    // this drives quick-switch combos, not an exhaustive language picker.
    return {
        {"zh-en", LangOptions::tr("Auto (Chinese ↔ English)")},
        {"to:zh-CN", LangOptions::tr("Chinese")},
        {"to:en", LangOptions::tr("English")},
        {"to:ja", LangOptions::tr("Japanese")},
        {"to:ko", LangOptions::tr("Korean")},
        {"to:fr", LangOptions::tr("French")},
        {"to:de", LangOptions::tr("German")},
        {"to:es", LangOptions::tr("Spanish")},
        {"to:ru", LangOptions::tr("Russian")},
        {"to:pt", LangOptions::tr("Portuguese")},
        {"to:it", LangOptions::tr("Italian")},
    };
}
} // namespace

namespace w2t::ui {

QString normalizedTargetRule(const QString &rule)
{
    if (rule == QLatin1String("to-zh"))
        return QStringLiteral("to:zh-CN");
    if (rule == QLatin1String("to-en"))
        return QStringLiteral("to:en");
    return rule.isEmpty() ? QStringLiteral("zh-en") : rule;
}

void populateTargetRuleCombo(QComboBox *combo)
{
    for (const Option &option : options())
        combo->addItem(option.name, QLatin1String(option.rule));
}

void selectTargetRule(QComboBox *combo, const QString &rule)
{
    combo->setCurrentIndex(qMax(0, combo->findData(normalizedTargetRule(rule))));
}

} // namespace w2t::ui
