#pragma once

#include "engines/TranslateTypes.h"
#include "storage/HistoryStore.h"
#include "storage/VocabStore.h"

#include <QObject>
#include <QString>

#include <memory>

class Storage;

// Facade over the history + vocabulary stores for the app/ui layers
// (DESIGN.md §8). Owns the single Storage connection and the two stores, so no
// other layer touches SQL directly. History writes honour the "pause recording"
// switch (DESIGN.md §8.2); vocabulary add/remove drives the popup star and the
// repeat-lookup lookup_count bump (DESIGN.md §8.3).
class LibraryService : public QObject {
    Q_OBJECT

public:
    explicit LibraryService(QObject *parent = nullptr);
    ~LibraryService() override;

    // True once the database opened and migrated. When false every method is a
    // safe no-op so the rest of the app keeps working without persistence.
    bool isReady() const;

    // Direct store access for the UI models (History / Vocabulary tabs).
    HistoryStore *history() const { return m_history.get(); }
    VocabStore *vocab() const { return m_vocab.get(); }

    // "Pause recording" quick switch (tray + settings). Backed by Settings so it
    // survives restarts; changes broadcast via recordingPausedChanged().
    bool isRecordingPaused() const;
    void setRecordingPaused(bool paused);

    // Record a translation into history (first successful result of a request).
    // No-op when recording is paused or history is disabled. Applies the
    // configured retention cap afterwards. trigger is one of
    // selection|capture|input|clipboard.
    void recordHistory(const QString &trigger, const TranslateRequest &request, const TranslateResult &result);

    // Whether (word, lang) is already in the vocabulary (popup star state).
    bool isSaved(const QString &word, const QString &lang) const;

    // Add a word to the vocabulary from a result + optional context sentence.
    // Bumps lookup_count on a repeat (DESIGN.md §8.3). Emits vocabularyChanged.
    void addToVocabulary(const TranslateResult &result, const QString &fallbackWord, const QString &context);

    // Remove a saved word. Emits vocabularyChanged.
    void removeFromVocabulary(const QString &word, const QString &lang);

signals:
    void recordingPausedChanged(bool paused);
    void historyChanged();
    void vocabularyChanged();

private:
    std::unique_ptr<Storage> m_storage;
    std::unique_ptr<HistoryStore> m_history;
    std::unique_ptr<VocabStore> m_vocab;
    bool m_ready = false;
};
