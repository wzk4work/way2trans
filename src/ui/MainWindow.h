#pragma once

#include "engines/TranslateTypes.h"

#include <QHash>
#include <QMainWindow>

class LibraryService;
class TranslateService;
class HistoryModel;
class VocabModel;
class EngineResultCard;
class QPlainTextEdit;
class QLineEdit;
class QListView;
class QVBoxLayout;
class QComboBox;
class QLabel;
class QTextBrowser;
class QModelIndex;

// Main input-translation window with three tabs (DESIGN.md §7):
// Translate / History / Vocabulary. History and Vocabulary consume
// QAbstractListModel-derived models over the storage layer; the Translate tab
// reuses the shared TranslateService and renders one EngineResultCard per
// engine. Services are injected so the window never owns platform/engine state.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Inject the shared services. Safe to pass nullptr (the window then shows
    // empty tabs); call before showing for full functionality.
    void setServices(TranslateService *translate, LibraryService *library);

    // Pre-fill the translate tab input and run it (history re-translation).
    void translateText(const QString &text);

signals:
    // A translation was requested from this window (input tab or a history
    // re-translation). The app tags it as the "input" trigger and dispatches it,
    // so history records the right source (DESIGN.md §8.2).
    void inputTranslationRequested(const QString &text);

private:
    void buildTranslateTab();
    void buildHistoryTab();
    void buildVocabTab();

    void runTranslation();
    void onRequestStarted(const TranslateRequest &request, const QStringList &engineIds);
    void onResultReady(const TranslateResult &result);
    void onPartialResult(const QString &engineId, const QString &delta);

    void onHistoryActivated(const QModelIndex &index);
    void showHistoryContextMenu(const QPoint &pos);

    void onVocabActivated(const QModelIndex &index);
    void cycleVocabStatus();
    void exportVocab(const QString &format);

    TranslateService *m_translate = nullptr;
    LibraryService *m_library = nullptr;

    // Translate tab.
    QPlainTextEdit *m_input = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    QHash<QString, EngineResultCard *> m_cards;

    // History tab.
    HistoryModel *m_historyModel = nullptr;
    QLineEdit *m_historySearch = nullptr;
    QListView *m_historyView = nullptr;

    // Vocabulary tab.
    VocabModel *m_vocabModel = nullptr;
    QListView *m_vocabView = nullptr;
    QComboBox *m_vocabSort = nullptr;
    QComboBox *m_vocabStatusFilter = nullptr;
    QTextBrowser *m_vocabDetail = nullptr;
    qint64 m_selectedVocabId = 0;
};
