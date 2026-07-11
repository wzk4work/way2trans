#include "SettingsSections.h"

#include "LangOptions.h"
#include "Settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
// tr() context for every string on these pages (lupdate groups by this name).
struct SettingsSections {
    Q_DECLARE_TR_FUNCTIONS(SettingsSections)
};

// Bind a QLineEdit to a Settings key: seed it and write back on edit.
void bindLineEdit(QLineEdit *edit, const QString &key, const QString &defaultValue = {})
{
    edit->setText(Settings::instance().value(key, defaultValue).toString());
    QObject::connect(
        edit, &QLineEdit::textChanged, edit, [key](const QString &text) { Settings::instance().setValue(key, text); });
}

} // namespace

namespace w2t::ui {

QWidget *buildGeneralPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *form = new QFormLayout(page);
    Settings &s = Settings::instance();

    // Target-language rule: the automatic Chinese ↔ English pair or a pinned
    // target (shared option list with the translate page and popup combos).
    // A pinned target also doubles as an escape hatch when script detection
    // misfires (e.g. OCR noise).
    auto *rule = new QComboBox(page);
    w2t::ui::populateTargetRuleCombo(rule);
    w2t::ui::selectTargetRule(
        rule, s.value(QStringLiteral("general/targetLangRule"), QStringLiteral("zh-en")).toString());
    QObject::connect(rule, &QComboBox::currentIndexChanged, page,
        [rule]() { Settings::instance().setValue(QStringLiteral("general/targetLangRule"), rule->currentData()); });
    form->addRow(SettingsSections::tr("Translate to:"), rule);

    auto *autostart = new QCheckBox(SettingsSections::tr("Start way2trans at login"), page);
    autostart->setChecked(s.autostart());
    QObject::connect(autostart, &QCheckBox::toggled, page, [](bool on) { Settings::instance().setAutostart(on); });
    form->addRow(autostart);

    // Bob-style convenience: the first successful result of every translation
    // lands in the clipboard (DESIGN.md §10 autoCopyResult).
    auto *autoCopy = new QCheckBox(SettingsSections::tr("Copy the first result to the clipboard automatically"), page);
    autoCopy->setChecked(s.value(QStringLiteral("general/autoCopyResult"), false).toBool());
    QObject::connect(autoCopy, &QCheckBox::toggled, page,
        [](bool on) { Settings::instance().setValue(QStringLiteral("general/autoCopyResult"), on); });
    form->addRow(autoCopy);

    // History retention.
    auto *historyEnabled = new QCheckBox(SettingsSections::tr("Keep translation history"), page);
    historyEnabled->setChecked(s.value(QStringLiteral("general/historyEnabled"), true).toBool());
    QObject::connect(historyEnabled, &QCheckBox::toggled, page,
        [](bool on) { Settings::instance().setValue(QStringLiteral("general/historyEnabled"), on); });
    form->addRow(historyEnabled);

    auto *limit = new QSpinBox(page);
    limit->setRange(0, 1000000);
    limit->setSpecialValueText(SettingsSections::tr("Unlimited")); // 0 = unlimited
    limit->setValue(s.value(QStringLiteral("general/historyLimit"), 1000).toInt());
    QObject::connect(limit, &QSpinBox::valueChanged, page,
        [](int v) { Settings::instance().setValue(QStringLiteral("general/historyLimit"), v); });
    form->addRow(SettingsSections::tr("History entries to keep:"), limit);

    auto *pause = new QCheckBox(SettingsSections::tr("Pause recording (privacy)"), page);
    pause->setChecked(s.value(QStringLiteral("general/pauseRecording"), false).toBool());
    QObject::connect(pause, &QCheckBox::toggled, page,
        [](bool on) { Settings::instance().setValue(QStringLiteral("general/pauseRecording"), on); });
    form->addRow(pause);

    return page;
}

QWidget *buildHotkeysPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;

    struct HotkeyRow {
        QString key;
        QString label;
        QString fallback;
    };
    const QList<HotkeyRow> rows = {
        {QStringLiteral("hotkeys/translate-selection"), SettingsSections::tr("Translate selection"),
            QStringLiteral("Ctrl+Alt+D")},
        {QStringLiteral("hotkeys/translate-capture"), SettingsSections::tr("Translate screenshot"),
            QStringLiteral("Ctrl+Alt+S")},
        {QStringLiteral("hotkeys/capture-ocr"), SettingsSections::tr("Screenshot OCR to clipboard"),
            QStringLiteral("Ctrl+Alt+Shift+S")},
        {QStringLiteral("hotkeys/show-main"), SettingsSections::tr("Show main window"), QString()},
        {QStringLiteral("hotkeys/translate-clipboard"), SettingsSections::tr("Translate clipboard"), QString()},
    };
    for (const HotkeyRow &r : rows) {
        auto *edit = new QKeySequenceEdit(page);
        edit->setKeySequence(QKeySequence(Settings::instance().value(r.key, r.fallback).toString()));
        QObject::connect(edit, &QKeySequenceEdit::keySequenceChanged, page, [key = r.key](const QKeySequence &seq) {
            Settings::instance().setValue(key, seq.toString(QKeySequence::PortableText));
        });
        form->addRow(r.label + QStringLiteral(":"), edit);
    }
    layout->addLayout(form);

    // Null-backend guidance (DESIGN.md §4.2): when global hotkeys can't be
    // grabbed, bind these CLI commands in the compositor/system settings.
    auto *guidance = new QGroupBox(SettingsSections::tr("If shortcuts don't work"), page);
    auto *gl = new QVBoxLayout(guidance);
    auto *guidanceLabel = new QLabel(
        SettingsSections::tr("On some Wayland setups way2trans can't grab global keys. Bind these commands in your "
                             "compositor or system shortcut settings instead:"),
        guidance);
    guidanceLabel->setWordWrap(true);
    gl->addWidget(guidanceLabel);
    auto addCmd = [gl, guidance](const QString &cmd) {
        auto *row = new QHBoxLayout;
        auto *field = new QLineEdit(cmd, guidance);
        field->setReadOnly(true);
        auto *copy = new QPushButton(SettingsSections::tr("Copy"), guidance);
        QObject::connect(copy, &QPushButton::clicked, field, [cmd]() { QApplication::clipboard()->setText(cmd); });
        row->addWidget(field);
        row->addWidget(copy);
        gl->addLayout(row);
    };
    addCmd(QStringLiteral("way2trans trigger selection"));
    addCmd(QStringLiteral("way2trans trigger capture"));
    addCmd(QStringLiteral("way2trans trigger capture-ocr"));
    layout->addWidget(guidance);
    layout->addStretch();
    return page;
}

QWidget *buildOcrPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *form = new QFormLayout(page);
    Settings &s = Settings::instance();

    auto *backend = new QComboBox(page);
    backend->addItem(SettingsSections::tr("Auto (prefer WeChat, fall back to Tesseract)"), QStringLiteral("auto"));
    backend->addItem(SettingsSections::tr("WeChat"), QStringLiteral("wechat"));
    backend->addItem(SettingsSections::tr("Tesseract"), QStringLiteral("tesseract"));
    const QString current = s.value(QStringLiteral("ocr/backend"), QStringLiteral("auto")).toString();
    backend->setCurrentIndex(qMax(0, backend->findData(current)));
    QObject::connect(backend, &QComboBox::currentIndexChanged, page,
        [backend]() { Settings::instance().setValue(QStringLiteral("ocr/backend"), backend->currentData()); });
    form->addRow(SettingsSections::tr("Default OCR backend:"), backend);

    auto *wechatPath = new QLineEdit(page);
    bindLineEdit(wechatPath, QStringLiteral("ocr/wechatPath"));
    wechatPath->setPlaceholderText(SettingsSections::tr("Auto-detect (leave empty)"));
    form->addRow(SettingsSections::tr("WeChat OCR path:"), wechatPath);

    return page;
}

QWidget *buildPlatformPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);

    auto *detected = new QGroupBox(SettingsSections::tr("Detected session"), page);
    auto *dl = new QFormLayout(detected);
    dl->addRow(SettingsSections::tr("Qt platform:"), new QLabel(QGuiApplication::platformName(), detected));
    layout->addWidget(detected);

    auto *overrides = new QGroupBox(SettingsSections::tr("Force backend (troubleshooting)"), page);
    auto *ol = new QFormLayout(overrides);
    Settings &s = Settings::instance();

    auto addOverride = [&](const QString &label, const QString &key, const QStringList &values) {
        auto *combo = new QComboBox(overrides);
        combo->addItem(SettingsSections::tr("Auto"), QString());
        for (const QString &v : values)
            combo->addItem(v, v);
        const QString cur = s.value(QStringLiteral("platform/%1").arg(key)).toString();
        combo->setCurrentIndex(qMax(0, combo->findData(cur)));
        QObject::connect(combo, &QComboBox::currentIndexChanged, overrides, [combo, key]() {
            Settings::instance().setValue(QStringLiteral("platform/%1").arg(key), combo->currentData().toString());
        });
        ol->addRow(label, combo);
    };
    addOverride(SettingsSections::tr("Selection:"), QStringLiteral("selection"),
        {QStringLiteral("x11"), QStringLiteral("wl-paste")});
    addOverride(SettingsSections::tr("Hotkey:"), QStringLiteral("hotkey"),
        {QStringLiteral("x11"), QStringLiteral("portal"), QStringLiteral("null")});
    addOverride(SettingsSections::tr("Popup:"), QStringLiteral("popup"),
        {QStringLiteral("x11"), QStringLiteral("layer-shell"), QStringLiteral("plain")});
    layout->addWidget(overrides);
    auto *overrideNote = new QLabel(SettingsSections::tr("Overrides take effect on the next launch."), page);
    overrideNote->setWordWrap(true);
    layout->addWidget(overrideNote);
    layout->addStretch();
    return page;
}

} // namespace w2t::ui
