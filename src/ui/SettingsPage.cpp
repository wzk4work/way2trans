#include "SettingsPage.h"

#include "Settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

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
void bindLineEdit(
    QLineEdit *edit, const QString &key, const QString &defaultValue = {}, std::function<void()> onChanged = {})
{
    edit->setText(Settings::instance().value(key, defaultValue).toString());
    QObject::connect(edit, &QLineEdit::textChanged, edit, [key, onChanged](const QString &text) {
        Settings::instance().setValue(key, text);
        if (onChanged)
            onChanged();
    });
}
} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Narrow section list on the left, stacked panes on the right — the layout
    // mainstream translators use for preferences, and it avoids nesting a
    // second tab bar inside the main window's navigation.
    auto *nav = new QListWidget(this);
    nav->setObjectName(QStringLiteral("settingsNav"));
    nav->setFixedWidth(108);
    nav->setFrameShape(QFrame::NoFrame);
    layout->addWidget(nav);

    auto *pages = new QStackedWidget(this);
    layout->addWidget(pages, 1);

    // Wrap each pane in a scroll area so a tall page (e.g. the services list)
    // scrolls instead of forcing a huge window minimum.
    auto scrolled = [this](QWidget *page) -> QWidget * {
        auto *area = new QScrollArea(this);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setWidget(page);
        return area;
    };
    const struct {
        QString label;
        QWidget *page;
    } sections[] = {
        {tr("General"), buildGeneralPage()},
        {tr("Hotkeys"), buildHotkeysPage()},
        {tr("Services"), buildServicesPage()},
        {tr("OCR"), buildOcrPage()},
        {tr("Platform"), buildPlatformPage()},
    };
    for (const auto &section : sections) {
        nav->addItem(section.label);
        pages->addWidget(scrolled(section.page));
    }
    connect(nav, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    nav->setCurrentRow(0);

    setStyleSheet(QStringLiteral(
        "#settingsNav { background: transparent; border: none; padding: 4px; font-size: 13px; }"
        "#settingsNav::item { padding: 7px 10px; border-radius: 8px; margin: 1px 2px; }"
        "#settingsNav::item:selected { background: palette(highlight); color: palette(highlighted-text); }"
        "#serviceRow {"
        "  background: palette(base);"
        "  border: 1px solid palette(midlight);"
        "  border-radius: 10px;"
        "}"
        "#serviceName { font-weight: 600; }"
        "#serviceWarn { color: palette(mid); font-size: 11px; }"));
}

QWidget *SettingsPage::buildGeneralPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    Settings &s = Settings::instance();

    // Target-language rule (one rule implemented in M1; kept as a visible knob).
    auto *rule = new QComboBox(page);
    rule->addItem(tr("Chinese ↔ English (auto)"), QStringLiteral("zh-en"));
    rule->setCurrentIndex(0);
    connect(rule, &QComboBox::currentIndexChanged, page,
        [rule]() { Settings::instance().setValue(QStringLiteral("general/targetLangRule"), rule->currentData()); });
    form->addRow(tr("Language pair rule:"), rule);

    auto *autostart = new QCheckBox(tr("Start way2trans at login"), page);
    autostart->setChecked(s.autostart());
    connect(autostart, &QCheckBox::toggled, page, [](bool on) { Settings::instance().setAutostart(on); });
    form->addRow(autostart);

    // History retention.
    auto *historyEnabled = new QCheckBox(tr("Keep translation history"), page);
    historyEnabled->setChecked(s.value(QStringLiteral("general/historyEnabled"), true).toBool());
    connect(historyEnabled, &QCheckBox::toggled, page,
        [](bool on) { Settings::instance().setValue(QStringLiteral("general/historyEnabled"), on); });
    form->addRow(historyEnabled);

    auto *limit = new QSpinBox(page);
    limit->setRange(0, 1000000);
    limit->setSpecialValueText(tr("Unlimited")); // 0 = unlimited
    limit->setValue(s.value(QStringLiteral("general/historyLimit"), 1000).toInt());
    connect(limit, &QSpinBox::valueChanged, page,
        [](int v) { Settings::instance().setValue(QStringLiteral("general/historyLimit"), v); });
    form->addRow(tr("History entries to keep:"), limit);

    auto *pause = new QCheckBox(tr("Pause recording (privacy)"), page);
    pause->setChecked(s.value(QStringLiteral("general/pauseRecording"), false).toBool());
    connect(pause, &QCheckBox::toggled, page,
        [](bool on) { Settings::instance().setValue(QStringLiteral("general/pauseRecording"), on); });
    form->addRow(pause);

    return page;
}

QWidget *SettingsPage::buildHotkeysPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
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
        auto *edit = new QKeySequenceEdit(page);
        edit->setKeySequence(QKeySequence(Settings::instance().value(r.key, r.fallback).toString()));
        connect(edit, &QKeySequenceEdit::keySequenceChanged, page, [key = r.key](const QKeySequence &seq) {
            Settings::instance().setValue(key, seq.toString(QKeySequence::PortableText));
        });
        form->addRow(r.label + QStringLiteral(":"), edit);
    }
    layout->addLayout(form);

    // Null-backend guidance (DESIGN.md §4.2): when global hotkeys can't be
    // grabbed, bind these CLI commands in the compositor/system settings.
    auto *guidance = new QGroupBox(tr("If shortcuts don't work"), page);
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
    return page;
}

QWidget *SettingsPage::buildServicesPage()
{
    auto *page = new QWidget(this);
    m_servicesLayout = new QVBoxLayout(page);
    m_servicesLayout->setSpacing(8);
    Settings &s = Settings::instance();

    auto *hint = new QLabel(tr("Results are shown in this order. Use the arrows to reorder."), page);
    hint->setObjectName(QStringLiteral("serviceWarn"));
    m_servicesLayout->addWidget(hint);

    // Render cards in the persisted display order (default = declaration order).
    QHash<QString, int> defaultOrder;
    for (int i = 0; i < engineInfos().size(); ++i)
        defaultOrder.insert(engineInfos().at(i).id, i);
    QList<EngineInfo> infos = engineInfos();
    std::stable_sort(infos.begin(), infos.end(), [&s, &defaultOrder](const EngineInfo &a, const EngineInfo &b) {
        return s.value(QStringLiteral("engines/%1/order").arg(a.id), defaultOrder.value(a.id)).toInt()
            < s.value(QStringLiteral("engines/%1/order").arg(b.id), defaultOrder.value(b.id)).toInt();
    });

    for (const EngineInfo &info : infos) {
        auto *card = new QFrame(page);
        card->setObjectName(QStringLiteral("serviceRow"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(10, 6, 10, 6);
        cardLayout->setSpacing(4);

        // --- header: expand arrow, name, config hint, toggle, reorder -----
        auto *header = new QHBoxLayout;
        auto *expand = new QToolButton(card);
        expand->setArrowType(Qt::RightArrow);
        expand->setCheckable(true);
        expand->setAutoRaise(true);
        expand->setToolTip(tr("Show configuration"));
        header->addWidget(expand);

        auto *name = new QLabel(info.name, card);
        name->setObjectName(QStringLiteral("serviceName"));
        header->addWidget(name);

        auto *warn = new QLabel(tr("API key required"), card);
        warn->setObjectName(QStringLiteral("serviceWarn"));
        warn->setVisible(
            info.keyGated && s.value(QStringLiteral("engines/%1/apiKey").arg(info.id)).toString().isEmpty());
        header->addWidget(warn);
        header->addStretch();

        const bool defaultEnabled = !info.keyGated; // free web engines ship on
        auto *enabled = new QCheckBox(tr("Enabled"), card);
        enabled->setChecked(s.value(QStringLiteral("engines/%1/enabled").arg(info.id), defaultEnabled).toBool());
        connect(enabled, &QCheckBox::toggled, card, [this, id = info.id](bool on) {
            Settings::instance().setValue(QStringLiteral("engines/%1/enabled").arg(id), on);
            emit enginesChanged();
        });
        header->addWidget(enabled);

        // Plain-text arrows, not arrowType triangles: the triangle glyphs are
        // the expand/collapse affordance, so reordering gets a distinct shape.
        auto *up = new QToolButton(card);
        up->setText(QStringLiteral("↑"));
        up->setAutoRaise(true);
        up->setToolTip(tr("Move up"));
        auto *down = new QToolButton(card);
        down->setText(QStringLiteral("↓"));
        down->setAutoRaise(true);
        down->setToolTip(tr("Move down"));
        connect(up, &QToolButton::clicked, this, [this, card]() { moveService(m_servicesLayout->indexOf(card), -1); });
        connect(
            down, &QToolButton::clicked, this, [this, card]() { moveService(m_servicesLayout->indexOf(card), +1); });
        header->addWidget(up);
        header->addWidget(down);
        cardLayout->addLayout(header);

        // --- body: collapsed by default, holds the engine's config form ----
        auto *body = new QWidget(card);
        auto *form = new QFormLayout(body);
        form->setContentsMargins(22, 2, 0, 2);
        body->hide();
        connect(expand, &QToolButton::toggled, body, [expand, body](bool on) {
            expand->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
            body->setVisible(on);
        });

        const auto notifyEngines = [this]() { emit enginesChanged(); };
        if (info.keyGated) {
            auto *keyEdit = new QLineEdit(body);
            keyEdit->setEchoMode(QLineEdit::Password);
            bindLineEdit(keyEdit, QStringLiteral("engines/%1/apiKey").arg(info.id), {}, notifyEngines);
            connect(keyEdit, &QLineEdit::textChanged, warn,
                [warn](const QString &text) { warn->setVisible(text.trimmed().isEmpty()); });
            form->addRow(tr("API key:"), keyEdit);
        }
        if (info.hasEndpoint) {
            auto *endpointEdit = new QLineEdit(body);
            const QString key = info.id == QLatin1String("google") ? QStringLiteral("engines/google/host")
                                                                   : QStringLiteral("engines/%1/endpoint").arg(info.id);
            const QString def
                = info.id == QLatin1String("llm") ? QStringLiteral("https://api.openai.com/v1") : QString();
            bindLineEdit(endpointEdit, key, def, notifyEngines);
            form->addRow(info.id == QLatin1String("google") ? tr("Host:") : tr("Endpoint:"), endpointEdit);
        }
        if (info.hasModel) {
            auto *modelEdit = new QLineEdit(body);
            bindLineEdit(modelEdit, QStringLiteral("engines/%1/model").arg(info.id), QStringLiteral("gpt-4o-mini"),
                notifyEngines);
            form->addRow(tr("Model:"), modelEdit);
        }
        if (info.hasPrompt) {
            auto *prompt = new QPlainTextEdit(body);
            prompt->setMaximumHeight(80);
            prompt->setPlaceholderText(tr("Prompt template — use {text} / {source} / {target}"));
            prompt->setPlainText(s.value(QStringLiteral("engines/%1/prompt").arg(info.id)).toString());
            connect(prompt, &QPlainTextEdit::textChanged, body, [this, prompt, id = info.id]() {
                Settings::instance().setValue(QStringLiteral("engines/%1/prompt").arg(id), prompt->toPlainText());
                emit enginesChanged();
            });
            form->addRow(tr("Prompt:"), prompt);
        }

        // Connectivity test button + inline result.
        auto *testRow = new QHBoxLayout;
        auto *testButton = new QPushButton(tr("Test connection"), body);
        auto *resultLabel = new QLabel(body);
        connect(testButton, &QPushButton::clicked, body,
            [this, id = info.id, resultLabel]() { testEngine(id, resultLabel); });
        testRow->addWidget(testButton);
        testRow->addWidget(resultLabel, 1);
        form->addRow(testRow);

        cardLayout->addWidget(body);
        m_servicesLayout->addWidget(card);
        m_serviceOrder.append(info.id);
    }
    m_servicesLayout->addStretch();
    return page;
}

void SettingsPage::moveService(int index, int delta)
{
    // Layout slots: [0] hint label, [1..n] cards, [n+1] stretch.
    const int first = 1;
    const int last = first + m_serviceOrder.size() - 1;
    const int target = index + delta;
    if (index < first || index > last || target < first || target > last)
        return;

    QLayoutItem *item = m_servicesLayout->takeAt(index);
    m_servicesLayout->insertItem(target, item);
    m_serviceOrder.move(index - first, target - first);
    persistServiceOrder();
    emit enginesChanged();
}

void SettingsPage::persistServiceOrder()
{
    for (int i = 0; i < m_serviceOrder.size(); ++i)
        Settings::instance().setValue(QStringLiteral("engines/%1/order").arg(m_serviceOrder.at(i)), i);
}

QWidget *SettingsPage::buildOcrPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    Settings &s = Settings::instance();

    auto *backend = new QComboBox(page);
    backend->addItem(tr("Auto (prefer WeChat, fall back to Tesseract)"), QStringLiteral("auto"));
    backend->addItem(tr("WeChat"), QStringLiteral("wechat"));
    backend->addItem(tr("Tesseract"), QStringLiteral("tesseract"));
    const QString current = s.value(QStringLiteral("ocr/backend"), QStringLiteral("auto")).toString();
    backend->setCurrentIndex(qMax(0, backend->findData(current)));
    connect(backend, &QComboBox::currentIndexChanged, page,
        [backend]() { Settings::instance().setValue(QStringLiteral("ocr/backend"), backend->currentData()); });
    form->addRow(tr("Default OCR backend:"), backend);

    auto *wechatPath = new QLineEdit(page);
    bindLineEdit(wechatPath, QStringLiteral("ocr/wechatPath"));
    wechatPath->setPlaceholderText(tr("Auto-detect (leave empty)"));
    form->addRow(tr("WeChat OCR path:"), wechatPath);

    return page;
}

QWidget *SettingsPage::buildPlatformPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *detected = new QGroupBox(tr("Detected session"), page);
    auto *dl = new QFormLayout(detected);
    dl->addRow(tr("Qt platform:"), new QLabel(QGuiApplication::platformName(), detected));
    layout->addWidget(detected);

    auto *overrides = new QGroupBox(tr("Force backend (troubleshooting)"), page);
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
    auto *overrideNote = new QLabel(tr("Overrides take effect on the next launch."), page);
    overrideNote->setWordWrap(true);
    layout->addWidget(overrideNote);
    layout->addStretch();
    return page;
}

void SettingsPage::testEngine(const QString &engineId, QLabel *resultLabel)
{
    if (!m_engineTester) {
        resultLabel->setText(tr("Testing unavailable"));
        return;
    }
    resultLabel->setText(tr("Testing…"));
    // QPointer: the async result may land after the card (or page) is gone.
    m_engineTester(engineId, [label = QPointer<QLabel>(resultLabel)](bool ok, QString message) {
        if (!label)
            return;
        label->setText(ok ? tr("OK: %1").arg(message) : tr("Failed: %1").arg(message));
    });
}
