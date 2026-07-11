#pragma once

#include <QString>

class QComboBox;

namespace w2t::ui {

// The target-language rule options shared by the settings page, the translate
// page and the popup (DESIGN.md §10 general/targetLangRule): "zh-en" is the
// automatic Chinese ↔ English pair, "to:<BCP-47>" pins the target language.
// Legacy stored values ("to-zh"/"to-en") normalise to the "to:" form.
QString normalizedTargetRule(const QString &rule);

// Fill `combo` with the rule options: "Auto (Chinese ↔ English)" first, then
// the fixed target languages. Item data holds the rule string.
void populateTargetRuleCombo(QComboBox *combo);

// Select the combo item matching `rule` (normalised first; falls back to the
// Auto item). Does not block signals — callers decide.
void selectTargetRule(QComboBox *combo, const QString &rule);

} // namespace w2t::ui
