#include "MainWindow.h"

#include "CardStyle.h"
#include "EngineResultCard.h"
#include "HistoryModel.h"
#include "LibraryService.h"
#include "TranslateService.h"
#include "VocabModel.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString engineDisplayName(const QString &id)
{
    if (id == QLatin1String("google"))
        return QStringLiteral("Google");
    if (id == QLatin1String("bing"))
        return QStringLiteral("Bing");
    if (id == QLatin1String("deepl"))
        return QStringLiteral("DeepL");
    if (id == QLatin1String("llm"))
        return QStringLiteral("LLM");
    return id;
}

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
    resize(720, 560);

    auto *tabs = new QTabWidget(this);
    setCentralWidget(tabs);

    auto *translateTab = new QWidget(tabs);
    auto *historyTab = new QWidget(tabs);
    auto *vocabTab = new QWidget(tabs);
    tabs->addTab(translateTab, tr("Translate"));
    tabs->addTab(historyTab, tr("History"));
    tabs->addTab(vocabTab, tr("Vocabulary"));

    // Settings entry point in the tab-bar corner. It opens the shared modal
    // dialog (5 pages) rather than being a nested tab, so we don't stack a
    // second tab bar inside a tab.
    auto *settingsButton = new QToolButton(tabs);
    settingsButton->setText(QStringLiteral("⚙ ") + tr("Settings…"));
    settingsButton->setAutoRaise(true);
    settingsButton->setCursor(Qt::PointingHandCursor);
    settingsButton->setToolTip(tr("Open settings"));
    connect(settingsButton, &QToolButton::clicked, this, &MainWindow::settingsRequested);
    tabs->setCornerWidget(settingsButton, Qt::TopRightCorner);

    // Lay out into the placeholder widgets created above.
    m_input = nullptr;
    {
        // Translate tab.
        auto *layout = new QVBoxLayout(translateTab);
        m_input = new QPlainTextEdit(translateTab);
        m_input->setPlaceholderText(tr("Enter text to translate…"));
        m_input->setMaximumHeight(120);
        layout->addWidget(m_input);

        auto *row = new QHBoxLayout;
        auto *translateButton = new QPushButton(tr("Translate"), translateTab);
        connect(translateButton, &QPushButton::clicked, this, &MainWindow::runTranslation);
        row->addStretch();
        row->addWidget(translateButton);
        layout->addLayout(row);

        auto *scroll = new QScrollArea(translateTab);
        scroll->setWidgetResizable(true);
        auto *cardsContainer = new QWidget;
        m_cardsLayout = new QVBoxLayout(cardsContainer);
        m_cardsLayout->addStretch();
        scroll->setWidget(cardsContainer);
        layout->addWidget(scroll, 1);
    }

    {
        // History tab.
        auto *layout = new QVBoxLayout(historyTab);
        m_historySearch = new QLineEdit(historyTab);
        m_historySearch->setPlaceholderText(tr("Search history…"));
        m_historySearch->setClearButtonEnabled(true);
        layout->addWidget(m_historySearch);

        m_historyView = new QListView(historyTab);
        m_historyView->setContextMenuPolicy(Qt::CustomContextMenu);
        layout->addWidget(m_historyView, 1);
    }

    {
        // Vocabulary tab.
        auto *layout = new QVBoxLayout(vocabTab);
        auto *controls = new QHBoxLayout;
        m_vocabSort = new QComboBox(vocabTab);
        m_vocabSort->addItem(tr("Most looked up"));
        m_vocabSort->addItem(tr("Recently added"));
        m_vocabStatusFilter = new QComboBox(vocabTab);
        m_vocabStatusFilter->addItem(tr("All statuses"), -1);
        m_vocabStatusFilter->addItem(tr("New"), 0);
        m_vocabStatusFilter->addItem(tr("Familiar"), 1);
        m_vocabStatusFilter->addItem(tr("Mastered"), 2);
        controls->addWidget(new QLabel(tr("Sort:"), vocabTab));
        controls->addWidget(m_vocabSort);
        controls->addWidget(new QLabel(tr("Filter:"), vocabTab));
        controls->addWidget(m_vocabStatusFilter);
        controls->addStretch();

        auto *ankiButton = new QPushButton(tr("Export Anki…"), vocabTab);
        auto *csvButton = new QPushButton(tr("Export CSV…"), vocabTab);
        auto *jsonButton = new QPushButton(tr("Export JSON…"), vocabTab);
        connect(ankiButton, &QPushButton::clicked, this, [this]() { exportVocab(QStringLiteral("anki")); });
        connect(csvButton, &QPushButton::clicked, this, [this]() { exportVocab(QStringLiteral("csv")); });
        connect(jsonButton, &QPushButton::clicked, this, [this]() { exportVocab(QStringLiteral("json")); });
        controls->addWidget(ankiButton);
        controls->addWidget(csvButton);
        controls->addWidget(jsonButton);
        layout->addLayout(controls);

        auto *split = new QHBoxLayout;
        m_vocabView = new QListView(vocabTab);
        split->addWidget(m_vocabView, 1);
        m_vocabDetail = new QTextBrowser(vocabTab);
        split->addWidget(m_vocabDetail, 1);
        layout->addLayout(split, 1);

        auto *statusButton = new QPushButton(tr("Cycle status (New → Familiar → Mastered)"), vocabTab);
        connect(statusButton, &QPushButton::clicked, this, &MainWindow::cycleVocabStatus);
        layout->addWidget(statusButton);
    }

    // Rounded, popup-consistent styling for inputs, lists and buttons; the
    // engine result cards reuse the exact popup card style (CardStyle.h).
    setStyleSheet(
        QStringLiteral("QPlainTextEdit, QLineEdit, QTextBrowser, QListView {"
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
                       "QListView::item { padding: 5px; border-radius: 6px; }"
                       "QListView::item:selected { background: palette(highlight); color: palette(highlighted-text); }"
                       "QScrollArea { border: none; background: transparent; }"
                       "QScrollArea > QWidget > QWidget { background: transparent; }"
                       // No rounded pane border: it sits flush to the square window
                       // corners and the mismatch shows. The inner widgets carry the
                       // rounding instead.
                       "QTabWidget::pane { border: none; }"
                       "QPushButton {"
                       "  background: palette(button);"
                       "  border: 1px solid palette(mid);"
                       "  border-radius: 8px;"
                       "  padding: 6px 14px;"
                       "}"
                       "QPushButton:hover { background: palette(midlight); }"
                       "QPushButton:pressed { background: palette(mid); }"
                       "QToolButton { border: none; border-radius: 6px; padding: 3px 8px; }"
                       "QToolButton:hover { background: palette(midlight); }")
        + w2t::ui::cardStyleSheet());
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
    connect(m_historyView, &QListView::activated, this, &MainWindow::onHistoryActivated);
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
        // Keep the tabs fresh when the library changes underneath us.
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
    // selection/screenshot translation doesn't hijack the translate tab.
    m_showingOwnRequest = m_ownRequestPending;
    m_ownRequestPending = false;
    if (!m_showingOwnRequest)
        return;

    // Rebuild the card set for this request.
    for (EngineResultCard *card : std::as_const(m_cards))
        card->deleteLater();
    m_cards.clear();

    for (const QString &id : engineIds) {
        auto *card = new EngineResultCard(id, engineDisplayName(id), this);
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
    // Single click / activation refills the translate tab and re-runs it.
    const HistoryRecord r = m_historyModel->recordAt(index.row());
    if (auto *tabs = qobject_cast<QTabWidget *>(centralWidget()))
        tabs->setCurrentIndex(0);
    translateText(r.sourceText);
}

void MainWindow::showHistoryContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_historyView->indexAt(pos);
    if (!index.isValid())
        return;
    const HistoryRecord r = m_historyModel->recordAt(index.row());

    QMenu menu(this);
    QAction *copySrc = menu.addAction(tr("Copy source"));
    QAction *copyRes = menu.addAction(tr("Copy translation"));
    QAction *favorite = menu.addAction(r.favorite ? tr("Unfavorite") : tr("Favorite"));
    QAction *addVocab = menu.addAction(tr("Add to vocabulary"));
    QAction *remove = menu.addAction(tr("Delete"));

    QAction *chosen = menu.exec(m_historyView->viewport()->mapToGlobal(pos));
    if (!chosen || !m_library || !m_library->isReady())
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
