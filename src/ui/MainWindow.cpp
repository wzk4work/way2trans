#include "MainWindow.h"

#include "CardStyle.h"
#include "EngineResultCard.h"
#include "HistoryItemDelegate.h"
#include "HistoryModel.h"
#include "LangOptions.h"
#include "LibraryService.h"
#include "ServicesPage.h"
#include "Settings.h"
#include "SettingsSections.h"
#include "TranslateService.h"
#include "VocabItemDelegate.h"
#include "VocabModel.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString statusLabel(int status)
{
    switch (status) {
    case 1:
        return MainWindow::tr("Familiar");
    case 2:
        return MainWindow::tr("Mastered");
    default:
        return MainWindow::tr("New");
    }
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("way2trans"));
    resize(820, 560);

    // Shell: one flat sidebar on the left, stacked pages on the right — the
    // layout mainstream translators use. The settings sections sit directly in
    // this sidebar under a group header, so there is no second-level nav.
    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(central);

    m_nav = new QListWidget(central);
    m_nav->setObjectName(QStringLiteral("mainNav"));
    m_nav->setFixedWidth(138);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setIconSize(QSize(16, 16));
    rootLayout->addWidget(m_nav);

    m_pages = new QStackedWidget(central);
    m_pages->setObjectName(QStringLiteral("mainPages"));
    rootLayout->addWidget(m_pages, 1);

    // Tall pages (services list, hotkey guidance) scroll instead of forcing a
    // large window minimum.
    auto scrolled = [this](QWidget *page) -> QWidget * {
        auto *area = new QScrollArea(this);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setWidget(page);
        return area;
    };
    // Nav icons come from the system icon theme so they follow the user's
    // desktop (Breeze, Adwaita, Papirus, …). Name coverage differs between
    // themes, so each entry tries a list of freedesktop names; the QStyle
    // standard icon guarantees something renders even without a theme.
    auto navIcon = [this](std::initializer_list<const char *> names, QStyle::StandardPixmap fallback) {
        for (const char *name : names) {
            const QIcon icon = QIcon::fromTheme(QLatin1String(name));
            if (!icon.isNull())
                return icon;
        }
        return style()->standardIcon(fallback);
    };
    auto addPage = [this](const QIcon &icon, const QString &label, QWidget *page) {
        auto *item = new QListWidgetItem(icon, label, m_nav);
        item->setData(Qt::UserRole, m_pages->count());
        m_pages->addWidget(page);
    };

    m_servicesPage = new ServicesPage(central);

    addPage(navIcon({"translate", "translator", "preferences-desktop-locale"}, QStyle::SP_FileDialogContentsView),
        tr("Translate"), buildTranslatePage());
    addPage(
        navIcon({"view-history", "document-open-recent"}, QStyle::SP_BrowserReload), tr("History"), buildHistoryPage());
    addPage(navIcon({"accessories-dictionary", "address-book-new"}, QStyle::SP_FileDialogDetailedView),
        tr("Vocabulary"), buildVocabPage());

    // Non-selectable group header separating the settings sections.
    auto *header = new QListWidgetItem(tr("Settings"), m_nav);
    header->setFlags(Qt::NoItemFlags);
    QFont headerFont = font();
    headerFont.setPointSizeF(headerFont.pointSizeF() * 0.85);
    header->setFont(headerFont);

    m_firstSettingsRow = m_nav->count();
    addPage(navIcon({"preferences-system", "configure"}, QStyle::SP_FileDialogListView), tr("General"),
        scrolled(w2t::ui::buildGeneralPage(this)));
    addPage(navIcon({"preferences-desktop-keyboard", "input-keyboard"}, QStyle::SP_DesktopIcon), tr("Hotkeys"),
        scrolled(w2t::ui::buildHotkeysPage(this)));
    addPage(navIcon({"network-server", "applications-internet", "globe"}, QStyle::SP_DriveNetIcon), tr("Services"),
        scrolled(m_servicesPage));
    addPage(navIcon({"document-scan", "scanner", "camera-photo"}, QStyle::SP_FileDialogContentsView), tr("OCR"),
        scrolled(w2t::ui::buildOcrPage(this)));
    addPage(navIcon({"computer", "video-display"}, QStyle::SP_ComputerIcon), tr("Platform"),
        scrolled(w2t::ui::buildPlatformPage(this)));

    connect(m_nav, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *) {
        if (current)
            m_pages->setCurrentIndex(current->data(Qt::UserRole).toInt());
    });
    m_nav->setCurrentRow(0);

    // Rounded, popup-consistent styling for inputs, lists and buttons; the
    // engine result cards reuse the exact popup card style (CardStyle.h).
    setStyleSheet(
        QStringLiteral(
            "QPlainTextEdit, QLineEdit, QTextBrowser, #contentList {"
            "  background: palette(base);"
            "  border: 1px solid palette(midlight);"
            "  border-radius: 8px;"
            "  padding: 6px 8px;"
            "}"
            "QComboBox {"
            "  background: palette(base);"
            "  border: 1px solid palette(midlight);"
            "  border-radius: 8px;"
            "  padding: 4px 8px;"
            "}"
            // Styling QComboBox with a box model makes Qt render its
            // popup like a menu; without an explicit ::item:selected
            // rule the current row paints highlighted-text on the
            // base background — i.e. invisible white-on-white.
            "QComboBox QAbstractItemView {"
            "  background: palette(base);"
            "  border: 1px solid palette(midlight);"
            "}"
            "QComboBox::item:selected {"
            "  background: palette(highlight);"
            "  color: palette(highlighted-text);"
            "}"
            // Card styling for the history/vocab lists only; a bare
            // QListView selector would leak into combo popups (their
            // list is a QListView too) and wreck item geometry.
            "#contentList::item { padding: 5px; border-radius: 6px; }"
            "#contentList::item:selected { background: palette(highlight); color: palette(highlighted-text); }"
            "QScrollArea { border: none; background: transparent; }"
            "QScrollArea > QWidget > QWidget { background: transparent; }"
            "QPushButton {"
            "  background: palette(button);"
            "  border: 1px solid palette(mid);"
            "  border-radius: 8px;"
            "  padding: 6px 14px;"
            "}"
            "QPushButton:hover { background: palette(midlight); }"
            "QPushButton:pressed { background: palette(mid); }"
            "QToolButton { border: none; border-radius: 6px; padding: 3px 8px; }"
            "QToolButton:hover { background: palette(midlight); }"
            // Sidebar navigation: flat list on the window background
            // with a hairline splitting it from the page area.
            "#mainNav {"
            "  background: transparent;"
            "  border: none;"
            "  border-right: 1px solid palette(midlight);"
            "  padding: 6px 2px;"
            "  font-size: 13px;"
            "}"
            "#mainNav::item { padding: 9px 12px; border-radius: 8px; margin: 1px 4px; }"
            "#mainNav::item:selected { background: palette(highlight); color: palette(highlighted-text); }"
            // The settings group header: quiet label, extra gap above.
            "#mainNav::item:disabled { color: palette(mid); margin-top: 10px; padding: 2px 12px; }"
            // History/vocabulary list cards: the frame carries the rounded
            // border, the list inside is flat (same pattern as #inputCard).
            "#listCard {"
            "  background: palette(base);"
            "  border: 1px solid palette(midlight);"
            "  border-radius: 8px;"
            "}"
            "#historyList, #vocabList { background: transparent; border: none; }"
            // Input card: the rounded surface is the card, so the
            // text edit inside stays flat and transparent.
            "#inputCard {"
            "  background: palette(base);"
            "  border: 1px solid palette(midlight);"
            "  border-radius: 10px;"
            "}"
            "#translateInput { background: transparent; border: none; padding: 0; }"
            "#langHint { color: palette(mid); font-size: 11px; }"
            "QPushButton#primaryButton {"
            "  background: palette(highlight);"
            "  color: palette(highlighted-text);"
            "  border: none;"
            "  border-radius: 8px;"
            "  padding: 6px 18px;"
            "  font-weight: 600;"
            "}")
        + w2t::ui::cardStyleSheet());
}

QWidget *MainWindow::buildTranslatePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    // Input card: text on top, a toolbar row below with quick actions on the
    // left and the primary translate button on the right (Pot/Easydict layout).
    auto *inputCard = new QFrame(page);
    inputCard->setObjectName(QStringLiteral("inputCard"));
    auto *cardLayout = new QVBoxLayout(inputCard);
    cardLayout->setContentsMargins(10, 8, 10, 8);
    cardLayout->setSpacing(6);

    m_input = new QPlainTextEdit(inputCard);
    m_input->setObjectName(QStringLiteral("translateInput"));
    m_input->setPlaceholderText(tr("Enter text to translate…"));
    m_input->setMaximumHeight(110);
    m_input->setTabChangesFocus(true);
    m_input->installEventFilter(this); // Enter translates, Shift+Enter newline
    cardLayout->addWidget(m_input);

    auto *toolbar = new QHBoxLayout;
    auto addTool = [this, inputCard, toolbar](const QString &text, const QString &tip) {
        auto *button = new QToolButton(inputCard);
        button->setText(text);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(true);
        toolbar->addWidget(button);
        return button;
    };
    auto *copyButton = addTool(tr("Copy"), tr("Copy source text"));
    connect(copyButton, &QToolButton::clicked, this,
        [this]() { QApplication::clipboard()->setText(m_input->toPlainText()); });
    auto *pasteButton = addTool(tr("Paste"), tr("Replace with clipboard text"));
    connect(pasteButton, &QToolButton::clicked, this,
        [this]() { m_input->setPlainText(QApplication::clipboard()->text()); });
    auto *clearButton = addTool(tr("Clear"), tr("Clear the input"));
    connect(clearButton, &QToolButton::clicked, this, [this]() { m_input->clear(); });
    toolbar->addStretch();

    // Quick target-language switch, bound to the global rule (same setting as
    // Settings → General); takes effect from the next translation.
    auto *langCombo = new QComboBox(inputCard);
    w2t::ui::populateTargetRuleCombo(langCombo);
    w2t::ui::selectTargetRule(
        langCombo, Settings::instance().value(QStringLiteral("general/targetLangRule")).toString());
    langCombo->setToolTip(tr("Target language — applies from the next translation"));
    langCombo->setCursor(Qt::PointingHandCursor);
    connect(langCombo, &QComboBox::activated, this, [langCombo]() {
        Settings::instance().setValue(QStringLiteral("general/targetLangRule"), langCombo->currentData());
    });
    toolbar->addWidget(langCombo);

    auto *translateButton = new QPushButton(tr("Translate"), inputCard);
    translateButton->setObjectName(QStringLiteral("primaryButton"));
    translateButton->setCursor(Qt::PointingHandCursor);
    connect(translateButton, &QPushButton::clicked, this, &MainWindow::runTranslation);
    toolbar->addWidget(translateButton);
    cardLayout->addLayout(toolbar);
    layout->addWidget(inputCard);

    // Ctrl+Enter translates while typing in the input.
    auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_input);
    shortcut->setContext(Qt::WidgetShortcut);
    connect(shortcut, &QShortcut::activated, this, &MainWindow::runTranslation);

    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    auto *cardsContainer = new QWidget;
    m_cardsLayout = new QVBoxLayout(cardsContainer);
    m_cardsLayout->addStretch();
    scroll->setWidget(cardsContainer);
    layout->addWidget(scroll, 1);
    return page;
}

QWidget *MainWindow::buildHistoryPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    m_historySearch = new QLineEdit(page);
    m_historySearch->setPlaceholderText(tr("Search history…"));
    m_historySearch->setClearButtonEnabled(true);
    layout->addWidget(m_historySearch);

    // Card pattern (AGENTS.md): the frame owns the rounded border, the list
    // inside stays flat and unpadded so scrolling can't leave paint residue.
    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("listCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 4, 4, 4);

    m_historyView = new QListView(card);
    m_historyView->setObjectName(QStringLiteral("historyList"));
    m_historyView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_historyDelegate = new HistoryItemDelegate(m_historyView, m_historyView);
    m_historyView->setItemDelegate(m_historyDelegate);
    // The delegate sizes rows to the viewport; Adjust re-lays them out on
    // resize, so the horizontal scrollbar can go away entirely.
    m_historyView->setResizeMode(QListView::Adjust);
    m_historyView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_historyView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_historyView->setMouseTracking(true); // hover state for the delegate
    m_historyView->setFrameShape(QFrame::NoFrame);
    cardLayout->addWidget(m_historyView);
    layout->addWidget(card, 1);
    return page;
}

QWidget *MainWindow::buildVocabPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *controls = new QHBoxLayout;
    m_vocabSort = new QComboBox(page);
    m_vocabSort->addItem(tr("Most looked up"));
    m_vocabSort->addItem(tr("Recently added"));
    m_vocabStatusFilter = new QComboBox(page);
    m_vocabStatusFilter->addItem(tr("All statuses"), -1);
    m_vocabStatusFilter->addItem(tr("New"), 0);
    m_vocabStatusFilter->addItem(tr("Familiar"), 1);
    m_vocabStatusFilter->addItem(tr("Mastered"), 2);
    controls->addWidget(new QLabel(tr("Sort:"), page));
    controls->addWidget(m_vocabSort);
    controls->addWidget(new QLabel(tr("Filter:"), page));
    controls->addWidget(m_vocabStatusFilter);
    controls->addStretch();

    auto *ankiButton = new QPushButton(tr("Export Anki…"), page);
    auto *csvButton = new QPushButton(tr("Export CSV…"), page);
    auto *jsonButton = new QPushButton(tr("Export JSON…"), page);
    connect(ankiButton, &QPushButton::clicked, this, [this]() { exportVocab(QStringLiteral("anki")); });
    connect(csvButton, &QPushButton::clicked, this, [this]() { exportVocab(QStringLiteral("csv")); });
    connect(jsonButton, &QPushButton::clicked, this, [this]() { exportVocab(QStringLiteral("json")); });
    controls->addWidget(ankiButton);
    controls->addWidget(csvButton);
    controls->addWidget(jsonButton);
    layout->addLayout(controls);

    auto *split = new QHBoxLayout;
    // Same card pattern as the history page: the frame owns the rounded
    // border, the list inside stays flat and unpadded.
    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("listCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 4, 4, 4);

    m_vocabView = new QListView(card);
    m_vocabView->setObjectName(QStringLiteral("vocabList"));
    m_vocabView->setItemDelegate(new VocabItemDelegate(m_vocabView, m_vocabView));
    // The delegate sizes rows to the viewport; Adjust re-lays them out on
    // resize, so the horizontal scrollbar can go away entirely.
    m_vocabView->setResizeMode(QListView::Adjust);
    m_vocabView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_vocabView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_vocabView->setMouseTracking(true); // hover state for the delegate
    m_vocabView->setFrameShape(QFrame::NoFrame);
    cardLayout->addWidget(m_vocabView);
    split->addWidget(card, 1);
    m_vocabDetail = new QTextBrowser(page);
    split->addWidget(m_vocabDetail, 1);
    layout->addLayout(split, 1);

    auto *statusButton = new QPushButton(tr("Cycle status (New → Familiar → Mastered)"), page);
    connect(statusButton, &QPushButton::clicked, this, &MainWindow::cycleVocabStatus);
    layout->addWidget(statusButton);
    return page;
}

void MainWindow::setServices(TranslateService *translate, LibraryService *library)
{
    m_translate = translate;
    m_library = library;

    if (m_translate) {
        connect(m_translate, &TranslateService::requestStarted, this, &MainWindow::onRequestStarted);
        connect(m_translate, &TranslateService::resultReady, this, &MainWindow::onResultReady);
        connect(m_translate, &TranslateService::partialResult, this, &MainWindow::onPartialResult);
    }

    // Wire up the history/vocab models now that the library is known.
    m_historyModel = new HistoryModel(m_library, this);
    m_historyView->setModel(m_historyModel);
    connect(
        m_historySearch, &QLineEdit::textChanged, this, [this](const QString &text) { m_historyModel->search(text); });
    // Selecting a row expands it in place to the full stored source and
    // translation — viewing history never re-runs a request. Re-translation
    // is only the explicit context-menu action.
    connect(m_historyView->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex &current, const QModelIndex &) { m_historyDelegate->setExpandedIndex(current); });
    connect(m_historyView, &QListView::customContextMenuRequested, this, &MainWindow::showHistoryContextMenu);

    m_vocabModel = new VocabModel(m_library, this);
    m_vocabView->setModel(m_vocabModel);
    connect(m_vocabView, &QListView::clicked, this, &MainWindow::onVocabActivated);
    connect(m_vocabSort, &QComboBox::currentIndexChanged, this, [this](int idx) {
        m_vocabModel->setSort(idx == 0 ? VocabStore::Sort::LookupCountDesc : VocabStore::Sort::AddedAtDesc);
    });
    connect(m_vocabStatusFilter, &QComboBox::currentIndexChanged, this,
        [this]() { m_vocabModel->setStatusFilter(m_vocabStatusFilter->currentData().toInt()); });

    if (m_library) {
        // Keep the pages fresh when the library changes underneath us.
        connect(m_library, &LibraryService::historyChanged, m_historyModel, &HistoryModel::reload);
        connect(m_library, &LibraryService::vocabularyChanged, m_vocabModel, &VocabModel::reload);
    }
}

void MainWindow::translateText(const QString &text)
{
    if (m_input)
        m_input->setPlainText(text);
    runTranslation();
}

void MainWindow::showSettings()
{
    m_nav->setCurrentRow(m_firstSettingsRow);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Enter runs the translation, Shift+Enter inserts a newline (Bob/Pot
    // behaviour); Ctrl+Enter keeps working through its existing QShortcut.
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const bool isEnter = ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter;
        if (isEnter && ke->modifiers() == Qt::NoModifier) {
            runTranslation();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::runTranslation()
{
    if (!m_input)
        return;
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    // Mark the next request as ours, then route through the app so it is tagged
    // as an "input" translation. requestStarted fires synchronously from
    // translate(), so the flag is still set when onRequestStarted latches it.
    m_ownRequestPending = true;
    emit inputTranslationRequested(text);
}

void MainWindow::onRequestStarted(const TranslateRequest &request, const QStringList &engineIds)
{
    Q_UNUSED(request);
    // The service is shared: only render requests this window started, so a
    // selection/screenshot translation doesn't hijack the translate page.
    m_showingOwnRequest = m_ownRequestPending;
    m_ownRequestPending = false;
    if (!m_showingOwnRequest)
        return;

    // Rebuild the card set for this request.
    for (EngineResultCard *card : std::as_const(m_cards))
        card->deleteLater();
    m_cards.clear();

    for (const QString &id : engineIds) {
        const QString name = m_translate ? m_translate->engineDisplayName(id) : id;
        auto *card = new EngineResultCard(id, name, this);
        if (m_translate)
            connect(card, &EngineResultCard::retryRequested, m_translate, &TranslateService::retry);
        // Insert before the trailing stretch.
        m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
        m_cards.insert(id, card);
    }
}

void MainWindow::onResultReady(const TranslateResult &result)
{
    if (!m_showingOwnRequest)
        return;
    auto it = m_cards.constFind(result.engineId);
    if (it != m_cards.constEnd())
        it.value()->showResult(result, /*preferDict=*/false);
}

void MainWindow::onPartialResult(const QString &engineId, const QString &delta)
{
    if (!m_showingOwnRequest)
        return;
    auto it = m_cards.constFind(engineId);
    if (it != m_cards.constEnd())
        it.value()->appendStreamDelta(delta);
}

void MainWindow::onHistoryActivated(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    // Context-menu "Translate again": refill the translate page and re-run.
    const HistoryRecord r = m_historyModel->recordAt(index.row());
    m_nav->setCurrentRow(0);
    translateText(r.sourceText);
}

void MainWindow::showHistoryContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_historyView->indexAt(pos);
    if (!index.isValid())
        return;
    const HistoryRecord r = m_historyModel->recordAt(index.row());

    QMenu menu(this);
    QAction *retranslate = menu.addAction(tr("Translate again"));
    QAction *copySrc = menu.addAction(tr("Copy source"));
    QAction *copyRes = menu.addAction(tr("Copy translation"));
    QAction *favorite = menu.addAction(r.favorite ? tr("Unfavorite") : tr("Favorite"));
    QAction *addVocab = menu.addAction(tr("Add to vocabulary"));
    QAction *remove = menu.addAction(tr("Delete"));

    QAction *chosen = menu.exec(m_historyView->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == retranslate) {
        onHistoryActivated(index);
        return;
    }
    if (!m_library || !m_library->isReady())
        return;

    if (chosen == copySrc) {
        QApplication::clipboard()->setText(r.sourceText);
    } else if (chosen == copyRes) {
        QApplication::clipboard()->setText(r.resultText);
    } else if (chosen == favorite) {
        m_library->history()->setFavorite(r.id, !r.favorite);
        m_historyModel->reload();
    } else if (chosen == addVocab) {
        // No DictEntry snapshot here; store the plain translation as fallback.
        TranslateResult res;
        res.translatedText = r.resultText;
        res.detectedSourceLang = r.sourceLang;
        m_library->addToVocabulary(res, r.sourceText.trimmed(), r.sourceText);
    } else if (chosen == remove) {
        m_library->history()->remove(r.id);
        m_historyModel->reload();
    }
}

void MainWindow::onVocabActivated(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    const VocabRecord r = m_vocabModel->recordAt(index.row());
    m_selectedVocabId = r.id;

    QString html = QStringLiteral("<h2>%1</h2>").arg(r.word.toHtmlEscaped());
    if (!r.phonetic.isEmpty())
        html += QStringLiteral("<div style='color:gray'>/%1/</div>").arg(r.phonetic.toHtmlEscaped());
    html += QStringLiteral("<div><b>%1</b>: %2 &nbsp; <b>%3</b>: %4</div>")
                .arg(tr("Status"), statusLabel(r.status), tr("Looked up"), QString::number(r.lookupCount));
    if (r.dict.has_value()) {
        for (const DictMeaning &m : r.dict->meanings) {
            if (!m.partOfSpeech.isEmpty())
                html += QStringLiteral("<div style='color:#3a7'><i>%1</i></div>").arg(m.partOfSpeech.toHtmlEscaped());
            html += QStringLiteral("<ul>");
            for (const QString &def : m.definitions)
                html += QStringLiteral("<li>%1</li>").arg(def.toHtmlEscaped());
            html += QStringLiteral("</ul>");
        }
    } else if (!r.translation.isEmpty()) {
        html += QStringLiteral("<div>%1</div>").arg(r.translation.toHtmlEscaped());
    }
    if (!r.context.isEmpty())
        html += QStringLiteral("<div style='margin-top:8px;color:gray'>%1: %2</div>")
                    .arg(tr("Context"), r.context.toHtmlEscaped());
    if (!r.note.isEmpty())
        html += QStringLiteral("<div style='margin-top:4px'>%1: %2</div>").arg(tr("Note"), r.note.toHtmlEscaped());
    if (!r.tags.isEmpty())
        html += QStringLiteral("<div style='margin-top:4px'>%1: %2</div>")
                    .arg(tr("Tags"), r.tags.join(QStringLiteral(", ")).toHtmlEscaped());
    m_vocabDetail->setHtml(html);
}

void MainWindow::cycleVocabStatus()
{
    if (m_selectedVocabId <= 0 || !m_library || !m_library->isReady())
        return;
    auto found = std::optional<VocabRecord> {};
    for (int i = 0; i < m_vocabModel->rowCount(); ++i) {
        const VocabRecord r = m_vocabModel->recordAt(i);
        if (r.id == m_selectedVocabId) {
            found = r;
            break;
        }
    }
    if (!found)
        return;
    const int next = (found->status + 1) % 3; // New -> Familiar -> Mastered -> New
    m_library->vocab()->setStatus(m_selectedVocabId, next);
    m_vocabModel->reload();
}

void MainWindow::exportVocab(const QString &format)
{
    if (!m_library || !m_library->isReady())
        return;

    QString filter;
    QString suggested;
    if (format == QLatin1String("anki")) {
        filter = tr("Anki TSV (*.tsv *.txt)");
        suggested = QStringLiteral("way2trans-vocab.tsv");
    } else if (format == QLatin1String("csv")) {
        filter = tr("CSV (*.csv)");
        suggested = QStringLiteral("way2trans-vocab.csv");
    } else {
        filter = tr("JSON (*.json)");
        suggested = QStringLiteral("way2trans-vocab.json");
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export Vocabulary"), suggested, filter);
    if (path.isEmpty())
        return;

    QString content;
    if (format == QLatin1String("anki"))
        content = m_library->vocab()->exportAnkiTsv();
    else if (format == QLatin1String("csv"))
        content = m_library->vocab()->exportCsv();
    else
        content = m_library->vocab()->exportJson();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(content.toUtf8()) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1").arg(path));
        return;
    }
    QMessageBox::information(this, tr("Export complete"), tr("Exported to %1").arg(path));
}
