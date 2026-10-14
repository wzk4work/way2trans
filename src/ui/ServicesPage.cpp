#include "ServicesPage.h"

#include "ServiceInstances.h"
#include "Settings.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// Which config fields each engine type exposes. Kept local so ui/ never links
// engines/; the types mirror EngineRegistry.
struct FieldSpec {
    bool apiKey = false;
    bool endpoint = false;
    bool model = false;
    bool prompt = false;
    bool host = false;
};

FieldSpec fieldsForType(const QString &engineType)
{
    if (engineType == QLatin1String("google"))
        return {.host = true};
    if (engineType == QLatin1String("deepl"))
        return {.apiKey = true};
    if (engineType == QLatin1String("llm"))
        return {.apiKey = true, .endpoint = true, .model = true, .prompt = true};
    return {}; // bing and anything unknown: no knobs
}

QString typeLabel(const QString &engineType)
{
    if (engineType == QLatin1String("llm"))
        return QStringLiteral("LLM");
    if (engineType == QLatin1String("deepl"))
        return QStringLiteral("DeepL");
    // Remaining web engine types read fine capitalised.
    return engineType.left(1).toUpper() + engineType.mid(1);
}

// Bind a QLineEdit to a key inside an instance's config group.
void bindConfigEdit(QLineEdit *edit, const QString &group, const QString &field, std::function<void()> onChanged)
{
    const QString key = group + QLatin1Char('/') + field;
    edit->setText(Settings::instance().value(key).toString());
    QObject::connect(edit, &QLineEdit::textChanged, edit, [key, onChanged](const QString &text) {
        Settings::instance().setValue(key, text);
        if (onChanged)
            onChanged();
    });
}
} // namespace

ServicesPage::ServicesPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    auto *header = new QHBoxLayout;
    auto *hint = new QLabel(tr("Results are shown in this order. Use the arrows to reorder."), this);
    hint->setObjectName(QStringLiteral("serviceHint"));
    header->addWidget(hint);
    header->addStretch();

    // "Add service" menu: one entry per built-in template (Bob/Pot style —
    // the LLM engine ships as several presets: OpenAI, DeepSeek, Ollama, …).
    auto *addButton = new QPushButton(tr("Add service"), this);
    auto *menu = new QMenu(addButton);
    for (const ServiceTemplate &tpl : ServiceInstances::templates()) {
        QAction *action = menu->addAction(tpl.name);
        connect(action, &QAction::triggered, this, [this, tpl]() { addService(tpl); });
    }
    addButton->setMenu(menu);
    header->addWidget(addButton);
    layout->addLayout(header);

    m_cardsLayout = new QVBoxLayout;
    m_cardsLayout->setSpacing(8);
    layout->addLayout(m_cardsLayout);
    layout->addStretch();

    setStyleSheet(QStringLiteral("#serviceRow {"
                                 "  background: palette(base);"
                                 "  border: 1px solid palette(midlight);"
                                 "  border-radius: 10px;"
                                 "}"
                                 "#serviceName { font-weight: 600; }"
                                 "#serviceType {"
                                 "  color: palette(mid);"
                                 "  border: 1px solid palette(midlight);"
                                 "  border-radius: 6px;"
                                 "  padding: 0px 6px;"
                                 "  font-size: 10px;"
                                 "}"
                                 "#serviceHint, #serviceWarn { color: palette(mid); font-size: 11px; }"));

    // Idempotent legacy migration before the first read: the page may be
    // constructed before TranslateService (which also seeds), and the list
    // would otherwise show up empty until the next rebuild.
    ServiceInstances::ensureSeeded();
    rebuild();
}

void ServicesPage::rebuild(const QString &expandId)
{
    while (QLayoutItem *item = m_cardsLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_order.clear();

    const QList<ServiceInstance> instances = ServiceInstances::all();
    for (const ServiceInstance &inst : instances) {
        m_cardsLayout->addWidget(buildCard(inst, inst.id == expandId));
        m_order.append(inst.id);
    }

    if (instances.isEmpty()) {
        auto *empty = new QLabel(tr("No services configured. Use \"Add service\" to create one."), this);
        empty->setObjectName(QStringLiteral("serviceHint"));
        empty->setWordWrap(true);
        m_cardsLayout->addWidget(empty);
    }
}

QWidget *ServicesPage::buildCard(const ServiceInstance &inst, bool expanded)
{
    Settings &s = Settings::instance();
    const QString group = ServiceInstances::configGroup(inst.id);
    const FieldSpec fields = fieldsForType(inst.engineType);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("serviceRow"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 6, 10, 6);
    cardLayout->setSpacing(4);

    // --- header: expand, name, type tag, key hint, toggle, ↑↓, delete ------
    auto *header = new QHBoxLayout;
    auto *expand = new QToolButton(card);
    expand->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    expand->setCheckable(true);
    expand->setChecked(expanded);
    expand->setAutoRaise(true);
    expand->setToolTip(tr("Show configuration"));
    header->addWidget(expand);

    auto *name = new QLabel(inst.name, card);
    name->setObjectName(QStringLiteral("serviceName"));
    header->addWidget(name);

    auto *type = new QLabel(typeLabel(inst.engineType), card);
    type->setObjectName(QStringLiteral("serviceType"));
    type->setToolTip(tr("Engine type"));
    header->addWidget(type);

    auto *warn = new QLabel(tr("API key required"), card);
    warn->setObjectName(QStringLiteral("serviceWarn"));
    warn->setVisible(inst.keyGated && s.value(group + QStringLiteral("/apiKey")).toString().isEmpty());
    header->addWidget(warn);
    header->addStretch();

    auto *enabled = new QCheckBox(tr("Enabled"), card);
    enabled->setChecked(inst.enabled);
    connect(enabled, &QCheckBox::toggled, card, [this, id = inst.id](bool on) {
        ServiceInstances::setEnabled(id, on);
        emit enginesChanged();
    });
    header->addWidget(enabled);

    // Plain-text arrows, not arrowType triangles: the triangle glyphs are the
    // expand/collapse affordance, so reordering gets a distinct shape.
    auto *up = new QToolButton(card);
    up->setText(QStringLiteral("↑"));
    up->setAutoRaise(true);
    up->setToolTip(tr("Move up"));
    connect(up, &QToolButton::clicked, this, [this, id = inst.id]() { moveService(id, -1); });
    header->addWidget(up);
    auto *down = new QToolButton(card);
    down->setText(QStringLiteral("↓"));
    down->setAutoRaise(true);
    down->setToolTip(tr("Move down"));
    connect(down, &QToolButton::clicked, this, [this, id = inst.id]() { moveService(id, +1); });
    header->addWidget(down);

    auto *remove = new QToolButton(card);
    remove->setText(QStringLiteral("✕"));
    remove->setAutoRaise(true);
    remove->setToolTip(tr("Remove service"));
    connect(remove, &QToolButton::clicked, this, [this, id = inst.id, name]() { removeService(id, name->text()); });
    header->addWidget(remove);
    cardLayout->addLayout(header);

    // --- body: rename + engine-type config + connectivity test -------------
    auto *body = new QWidget(card);
    auto *form = new QFormLayout(body);
    form->setContentsMargins(22, 2, 0, 2);
    body->setVisible(expanded);
    connect(expand, &QToolButton::toggled, body, [expand, body](bool on) {
        expand->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(on);
    });

    auto *nameEdit = new QLineEdit(inst.name, body);
    connect(nameEdit, &QLineEdit::textChanged, body, [this, id = inst.id, name](const QString &text) {
        ServiceInstances::rename(id, text);
        name->setText(text);
        emit enginesChanged();
    });
    form->addRow(tr("Name:"), nameEdit);

    const auto notify = [this]() { emit enginesChanged(); };
    if (fields.apiKey) {
        auto *keyEdit = new QLineEdit(body);
        keyEdit->setEchoMode(QLineEdit::Password);
        bindConfigEdit(keyEdit, group, QStringLiteral("apiKey"), notify);
        if (inst.keyGated) {
            connect(keyEdit, &QLineEdit::textChanged, warn,
                [warn](const QString &text) { warn->setVisible(text.trimmed().isEmpty()); });
        }
        form->addRow(tr("API key:"), keyEdit);
    }
    if (fields.endpoint) {
        auto *endpointEdit = new QLineEdit(body);
        bindConfigEdit(endpointEdit, group, QStringLiteral("endpoint"), notify);
        endpointEdit->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));
        form->addRow(tr("Endpoint:"), endpointEdit);
    }
    if (fields.model) {
        auto *modelEdit = new QLineEdit(body);
        bindConfigEdit(modelEdit, group, QStringLiteral("model"), notify);
        form->addRow(tr("Model:"), modelEdit);
    }
    if (fields.host) {
        auto *hostEdit = new QLineEdit(body);
        bindConfigEdit(hostEdit, group, QStringLiteral("host"), notify);
        hostEdit->setPlaceholderText(tr("Default host (leave empty)"));
        form->addRow(tr("Host:"), hostEdit);
    }
    if (fields.prompt) {
        auto *prompt = new QPlainTextEdit(body);
        prompt->setMaximumHeight(80);
        prompt->setPlaceholderText(tr("Prompt template — use {text} / {source} / {target}"));
        prompt->setPlainText(s.value(group + QStringLiteral("/prompt")).toString());
        connect(prompt, &QPlainTextEdit::textChanged, body, [this, prompt, group]() {
            Settings::instance().setValue(group + QStringLiteral("/prompt"), prompt->toPlainText());
            emit enginesChanged();
        });
        form->addRow(tr("Prompt:"), prompt);
    }

    auto *testRow = new QHBoxLayout;
    auto *testButton = new QPushButton(tr("Test connection"), body);
    auto *resultLabel = new QLabel(body);
    connect(
        testButton, &QPushButton::clicked, body, [this, id = inst.id, resultLabel]() { testService(id, resultLabel); });
    testRow->addWidget(testButton);
    testRow->addWidget(resultLabel, 1);
    form->addRow(testRow);

    cardLayout->addWidget(body);
    return card;
}

void ServicesPage::addService(const ServiceTemplate &tpl)
{
    const QString id = ServiceInstances::add(tpl);
    rebuild(/*expandId=*/id);
    emit enginesChanged();
}

void ServicesPage::removeService(const QString &id, const QString &name)
{
    const auto answer = QMessageBox::question(this, tr("Remove service"),
        tr("Remove \"%1\"? Its configuration (including any API key) will be deleted.").arg(name));
    if (answer != QMessageBox::Yes)
        return;
    ServiceInstances::remove(id);
    rebuild();
    emit enginesChanged();
}

void ServicesPage::moveService(const QString &id, int delta)
{
    const int index = m_order.indexOf(id);
    const int target = index + delta;
    if (index < 0 || target < 0 || target >= m_order.size())
        return;
    QLayoutItem *item = m_cardsLayout->takeAt(index);
    m_cardsLayout->insertItem(target, item);
    m_order.move(index, target);
    ServiceInstances::setOrder(m_order);
    emit enginesChanged();
}

void ServicesPage::testService(const QString &id, QLabel *resultLabel)
{
    if (!m_engineTester) {
        resultLabel->setText(tr("Testing unavailable"));
        return;
    }
    resultLabel->setText(tr("Testing…"));
    // QPointer: the async result may land after the card (or page) is gone.
    m_engineTester(id, [label = QPointer<QLabel>(resultLabel)](bool ok, QString message) {
        if (!label)
            return;
        label->setText(ok ? tr("OK: %1").arg(message) : tr("Failed: %1").arg(message));
    });
}
