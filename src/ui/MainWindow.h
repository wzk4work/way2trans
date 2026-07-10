#pragma once

#include "engines/TranslateTypes.h"

#include <QHash>
#include <QMainWindow>

class LibraryService;
class TranslateService;
class HistoryModel;
class VocabModel;
class EngineResultCard;
class ServicesPage;
class QPlainTextEdit;
class QLineEdit;
class QListView;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;
class QComboBox;
class QLabel;
class QTextBrowser;
class QModelIndex;

// Main window (DESIGN.md §7): a single flat navigation sidebar on the left —
// Translate / History / Vocabulary, then a "Settings" group with General /
// Hotkeys / Services / OCR / Platform — over a stacked page area, following
// the layout of mainstream translators (Pot, STranslate). History and
// Vocabulary consume QAbstractListModel-derived models over the storage
// layer; the Translate page reuses the shared TranslateService and renders
// one EngineResultCard per engine. Services are injected so the window never
// owns platform/engine state.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Inject the shared services. Safe to pass nullptr (the window then shows
    // empty pages); call before showing for full functionality.
    void setServices(TranslateService *translate, LibraryService *library);

    // Pre-fill the translate page input and run it (history re-translation).
    void translateText(const QString &text);

    // Switch to the first settings page (tray "Settings…" and similar entry
    // points).
    void showSettings();

    // The embedded services page; the app injects the engine connectivity
    // tester and listens for engine config changes through it.
    ServicesPage *servicesPage() const { return m_servicesPage; }

signals:
    // A translation was requested from this window (translate page or a history
    // re-translation). The app tags it as the "input" trigger and dispatches it,
    // so history records the right source (DESIGN.md §8.2).
    void inputTranslationRequested(const QString &text);

protected:
    // Enter in the translate input runs the translation; Shift+Enter inserts a
    // newline (Bob/Pot behaviour, DESIGN.md §3.3).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildTranslatePage();
    QWidget *buildHistoryPage();
    QWidget *buildVocabPage();

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

    // Shell: sidebar navigation + stacked pages. Nav items carry their page
    // index in Qt::UserRole (the "Settings" group header is not selectable).
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_pages = nullptr;
    ServicesPage *m_servicesPage = nullptr;
    int m_firstSettingsRow = 0; // nav row of the first settings entry

    // Translate page.
    QPlainTextEdit *m_input = nullptr;
    QVBoxLayout *m_cardsLayout = nullptr;
    QHash<QString, EngineResultCard *> m_cards;
    // The TranslateService is shared with the selection/screenshot flows. These
    // flags let the page render only translations it started itself, ignoring
    // results from popup-driven requests. Set just before we dispatch; latched
    // in onRequestStarted (which fires synchronously from translate()).
    bool m_ownRequestPending = false;
    bool m_showingOwnRequest = false;

    // History page.
    HistoryModel *m_historyModel = nullptr;
    QLineEdit *m_historySearch = nullptr;
    QListView *m_historyView = nullptr;

    // Vocabulary page.
    VocabModel *m_vocabModel = nullptr;
    QListView *m_vocabView = nullptr;
    QComboBox *m_vocabSort = nullptr;
    QComboBox *m_vocabStatusFilter = nullptr;
    QTextBrowser *m_vocabDetail = nullptr;
    qint64 m_selectedVocabId = 0;
};
