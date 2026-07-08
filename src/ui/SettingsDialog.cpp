#include "SettingsDialog.h"

#include "Settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
// The built-in engines, mirrored locally so ui/ need not link engines/.
struct EngineInfo {
    QString id;
    QString name;
    bool keyGated; // needs an API key / endpoint config
    bool hasEndpoint; // exposes a host/endpoint field
    bool hasModel; // exposes a model field
    bool hasPrompt; // exposes an editable prompt template
};

const QList<EngineInfo> &engineInfos()
{
    static const QList<EngineInfo> kEngines = {
        {QStringLiteral("google"), QStringLiteral("Google"), false, true, false, false},
        {QStringLiteral("bing"), QStringLiteral("Bing"), false, false, false, false},
        {QStringLiteral("deepl"), QStringLiteral("DeepL"), true, false, false, false},
        {QStringLiteral("llm"), QStringLiteral("LLM"), true, true, true, true},
    };
    return kEngines;
}

// Bind a QLineEdit to a Settings key: seed it and write back on edit.
void bindLineEdit(QLineEdit *edit, const QString &key, const QString &defaultValue = {})
{
    edit->setText(Settings::instance().value(key, defaultValue).toString());
    QObject::connect(
        edit, &QLineEdit::textChanged, edit, [key](const QString &text) { Settings::instance().setValue(key, text); });
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    resize(560, 480);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    // Wrap each page in a scroll area so a tall/wide page (e.g. the engines
    // list) scrolls instead of forcing a huge dialog minimum — otherwise the
    // window can't be resized smaller than the content.
    auto scrolled = [this](QWidget *page) -> QWidget * {
        auto *area = new QScrollArea(this);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setWidget(page);
        return area;
    };
    tabs->addTab(scrolled(buildGeneralTab()), tr("General"));
    tabs->addTab(scrolled(buildHotkeysTab()), tr("Hotkeys"));
    tabs->addTab(scrolled(buildEnginesTab()), tr("Engines"));
    tabs->addTab(scrolled(buildOcrTab()), tr("OCR"));
    tabs->addTab(scrolled(buildPlatformTab()), tr("Platform"));
    layout->addWidget(tabs);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    auto *closeButton = new QPushButton(tr("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);
}

QWidget *SettingsDialog::buildGeneralTab()
{
    auto *tab = new QWidget(this);
    auto *form = new QFormLayout(tab);
    Settings &s = Settings::instance();

    // Target-language rule (one rule implemented in M1; kept as a visible knob).
    auto *rule = new QComboBox(tab);
    rule->addItem(tr("Chinese ↔ English (auto)"), QStringLiteral("zh-en"));
    rule->setCurrentIndex(0);
    connect(rule, &QComboBox::currentIndexChanged, tab,
        [rule]() { Settings::instance().setValue(QStringLiteral("general/targetLangRule"), rule->currentData()); });
    form->addRow(tr("Language pair rule:"), rule);

    auto *autostart = new QCheckBox(tr("Start way2trans at login"), tab);
    autostart->setChecked(s.autostart());
    connect(autostart, &QCheckBox::toggled, tab, [](bool on) { Settings::instance().setAutostart(on); });
    form->addRow(autostart);

    // History retention.
    auto *historyEnabled = new QCheckBox(tr("Keep translation history"), tab);
    historyEnabled->setChecked(s.value(QStringLiteral("general/historyEnabled"), true).toBool());
    connect(historyEnabled, &QCheckBox::toggled, tab,
        [](bool on) { Settings::instance().setValue(QStringLiteral("general/historyEnabled"), on); });
    form->addRow(historyEnabled);

    auto *limit = new QSpinBox(tab);
    limit->setRange(0, 1000000);
    limit->setSpecialValueText(tr("Unlimited")); // 0 = unlimited
    limit->setValue(s.value(QStringLiteral("general/historyLimit"), 1000).toInt());
    connect(limit, &QSpinBox::valueChanged, tab,
        [](int v) { Settings::instance().setValue(QStringLiteral("general/historyLimit"), v); });
    form->addRow(tr("History entries to keep:"), limit);

    auto *pause = new QCheckBox(tr("Pause recording (privacy)"), tab);
    pause->setChecked(s.value(QStringLiteral("general/pauseRecording"), false).toBool());
    connect(pause, &QCheckBox::toggled, tab,
        [](bool on) { Settings::instance().setValue(QStringLiteral("general/pauseRecording"), on); });
    form->addRow(pause);

    return tab;
}

QWidget *SettingsDialog::buildHotkeysTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    auto *form = new QFormLayout;

    struct HotkeyRow {
        QString key;
        QString label;
        QString fallback;
    };
    const QList<HotkeyRow> rows = {
        {QStringLiteral("hotkeys/translate-selection"), tr("Translate selection"), QStringLiteral("Ctrl+Alt+D")},
        {QStringLiteral("hotkeys/translate-capture"), tr("Translate screenshot"), QStringLiteral("Ctrl+Alt+S")},
        {QStringLiteral("hotkeys/show-main"), tr("Show main window"), QString()},
        {QStringLiteral("hotkeys/translate-clipboard"), tr("Translate clipboard"), QString()},
    };
    for (const HotkeyRow &r : rows) {
        auto *edit = new QKeySequenceEdit(tab);
        edit->setKeySequence(QKeySequence(Settings::instance().value(r.key, r.fallback).toString()));
        connect(edit, &QKeySequenceEdit::keySequenceChanged, tab, [key = r.key](const QKeySequence &seq) {
            Settings::instance().setValue(key, seq.toString(QKeySequence::PortableText));
        });
        form->addRow(r.label + QStringLiteral(":"), edit);
    }
    layout->addLayout(form);

    // Null-backend guidance (DESIGN.md §4.2): when global hotkeys can't be
    // grabbed, bind these CLI commands in the compositor/system settings.
    auto *guidance = new QGroupBox(tr("If shortcuts don't work"), tab);
    auto *gl = new QVBoxLayout(guidance);
    auto *guidanceLabel
        = new QLabel(tr("On some Wayland setups way2trans can't grab global keys. Bind these commands in your "
                        "compositor or system shortcut settings instead:"),
            guidance);
    guidanceLabel->setWordWrap(true);
    gl->addWidget(guidanceLabel);
    auto addCmd = [gl, guidance](const QString &cmd) {
        auto *row = new QHBoxLayout;
        auto *field = new QLineEdit(cmd, guidance);
        field->setReadOnly(true);
        auto *copy = new QPushButton(tr("Copy"), guidance);
        QObject::connect(copy, &QPushButton::clicked, field, [cmd]() { QApplication::clipboard()->setText(cmd); });
        row->addWidget(field);
        row->addWidget(copy);
        gl->addLayout(row);
    };
    addCmd(QStringLiteral("way2trans trigger selection"));
    addCmd(QStringLiteral("way2trans trigger capture"));
    layout->addWidget(guidance);
    layout->addStretch();
    return tab;
}

QWidget *SettingsDialog::buildEnginesTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    Settings &s = Settings::instance();

    int order = 0;
    for (const EngineInfo &info : engineInfos()) {
        auto *box = new QGroupBox(info.name, tab);
        auto *form = new QFormLayout(box);

        const bool defaultEnabled = !info.keyGated; // free web engines ship on
        auto *enabled = new QCheckBox(tr("Enabled"), box);
        enabled->setChecked(s.value(QStringLiteral("engines/%1/enabled").arg(info.id), defaultEnabled).toBool());
        connect(enabled, &QCheckBox::toggled, box, [id = info.id](bool on) {
            Settings::instance().setValue(QStringLiteral("engines/%1/enabled").arg(id), on);
        });
        form->addRow(enabled);

        auto *orderSpin = new QSpinBox(box);
        orderSpin->setRange(0, 99);
        orderSpin->setValue(s.value(QStringLiteral("engines/%1/order").arg(info.id), order).toInt());
        connect(orderSpin, &QSpinBox::valueChanged, box,
            [id = info.id](int v) { Settings::instance().setValue(QStringLiteral("engines/%1/order").arg(id), v); });
        form->addRow(tr("Order:"), orderSpin);

        if (info.keyGated) {
            auto *keyEdit = new QLineEdit(box);
            keyEdit->setEchoMode(QLineEdit::Password);
            bindLineEdit(keyEdit, QStringLiteral("engines/%1/apiKey").arg(info.id));
            form->addRow(tr("API key:"), keyEdit);
        }
        if (info.hasEndpoint) {
            auto *endpointEdit = new QLineEdit(box);
            const QString key = info.id == QLatin1String("google") ? QStringLiteral("engines/google/host")
                                                                   : QStringLiteral("engines/%1/endpoint").arg(info.id);
            const QString def
                = info.id == QLatin1String("llm") ? QStringLiteral("https://api.openai.com/v1") : QString();
            bindLineEdit(endpointEdit, key, def);
            form->addRow(info.id == QLatin1String("google") ? tr("Host:") : tr("Endpoint:"), endpointEdit);
        }
        if (info.hasModel) {
            auto *modelEdit = new QLineEdit(box);
            bindLineEdit(modelEdit, QStringLiteral("engines/%1/model").arg(info.id), QStringLiteral("gpt-4o-mini"));
            form->addRow(tr("Model:"), modelEdit);
        }
        if (info.hasPrompt) {
            auto *prompt = new QPlainTextEdit(box);
            prompt->setMaximumHeight(80);
            prompt->setPlaceholderText(tr("Prompt template — use {text} / {source} / {target}"));
            prompt->setPlainText(s.value(QStringLiteral("engines/%1/prompt").arg(info.id)).toString());
            connect(prompt, &QPlainTextEdit::textChanged, box, [prompt, id = info.id]() {
                Settings::instance().setValue(QStringLiteral("engines/%1/prompt").arg(id), prompt->toPlainText());
            });
            form->addRow(tr("Prompt:"), prompt);
        }

        // Connectivity test button + inline result.
        auto *testRow = new QHBoxLayout;
        auto *testButton = new QPushButton(tr("Test connection"), box);
        auto *resultLabel = new QLabel(box);
        connect(testButton, &QPushButton::clicked, box,
            [this, id = info.id, resultLabel]() { testEngine(id, resultLabel); });
        testRow->addWidget(testButton);
        testRow->addWidget(resultLabel, 1);
        form->addRow(testRow);

        layout->addWidget(box);
        ++order;
    }
    layout->addStretch();
    return tab;
}

QWidget *SettingsDialog::buildOcrTab()
{
    auto *tab = new QWidget(this);
    auto *form = new QFormLayout(tab);
    Settings &s = Settings::instance();

    auto *backend = new QComboBox(tab);
    backend->addItem(tr("Auto (prefer WeChat, fall back to Tesseract)"), QStringLiteral("auto"));
    backend->addItem(tr("WeChat"), QStringLiteral("wechat"));
    backend->addItem(tr("Tesseract"), QStringLiteral("tesseract"));
    const QString current = s.value(QStringLiteral("ocr/backend"), QStringLiteral("auto")).toString();
    backend->setCurrentIndex(qMax(0, backend->findData(current)));
    connect(backend, &QComboBox::currentIndexChanged, tab,
        [backend]() { Settings::instance().setValue(QStringLiteral("ocr/backend"), backend->currentData()); });
    form->addRow(tr("Default OCR backend:"), backend);

    auto *wechatPath = new QLineEdit(tab);
    bindLineEdit(wechatPath, QStringLiteral("ocr/wechatPath"));
    wechatPath->setPlaceholderText(tr("Auto-detect (leave empty)"));
    form->addRow(tr("WeChat OCR path:"), wechatPath);

    return tab;
}

QWidget *SettingsDialog::buildPlatformTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    auto *detected = new QGroupBox(tr("Detected session"), tab);
    auto *dl = new QFormLayout(detected);
    dl->addRow(tr("Qt platform:"), new QLabel(QGuiApplication::platformName(), detected));
    layout->addWidget(detected);

    auto *overrides = new QGroupBox(tr("Force backend (troubleshooting)"), tab);
    auto *ol = new QFormLayout(overrides);
    Settings &s = Settings::instance();

    auto addOverride = [&](const QString &label, const QString &key, const QStringList &values) {
        auto *combo = new QComboBox(overrides);
        combo->addItem(tr("Auto"), QString());
        for (const QString &v : values)
            combo->addItem(v, v);
        const QString cur = s.value(QStringLiteral("platform/%1").arg(key)).toString();
        combo->setCurrentIndex(qMax(0, combo->findData(cur)));
        QObject::connect(combo, &QComboBox::currentIndexChanged, overrides, [combo, key]() {
            Settings::instance().setValue(QStringLiteral("platform/%1").arg(key), combo->currentData().toString());
        });
        ol->addRow(label, combo);
    };
    addOverride(tr("Selection:"), QStringLiteral("selection"), {QStringLiteral("x11"), QStringLiteral("wl-paste")});
    addOverride(tr("Hotkey:"), QStringLiteral("hotkey"),
        {QStringLiteral("x11"), QStringLiteral("portal"), QStringLiteral("null")});
    addOverride(tr("Popup:"), QStringLiteral("popup"),
        {QStringLiteral("x11"), QStringLiteral("layer-shell"), QStringLiteral("plain")});
    layout->addWidget(overrides);
    auto *overrideNote = new QLabel(tr("Overrides take effect on the next launch."), tab);
    overrideNote->setWordWrap(true);
    layout->addWidget(overrideNote);
    layout->addStretch();
    return tab;
}

void SettingsDialog::testEngine(const QString &engineId, QLabel *resultLabel)
{
    if (!m_engineTester) {
        resultLabel->setText(tr("Testing unavailable"));
        return;
    }
    resultLabel->setText(tr("Testing…"));
    m_engineTester(engineId, [resultLabel](bool ok, QString message) {
        if (!resultLabel)
            return;
        resultLabel->setText(ok ? tr("OK: %1").arg(message) : tr("Failed: %1").arg(message));
    });
}
