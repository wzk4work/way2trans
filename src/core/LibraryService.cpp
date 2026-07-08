#include "LibraryService.h"

#include "LangDetector.h"
#include "Logging.h"
#include "Settings.h"
#include "storage/Storage.h"

namespace {
constexpr auto kKeyRecordingPaused = "general/pauseRecording";
// History retention cap; 0 (or negative) means "unlimited" (DESIGN.md §8.2).
constexpr auto kKeyHistoryLimit = "general/historyLimit";
constexpr int kDefaultHistoryLimit = 1000;
// A master switch to turn history off entirely (DESIGN.md §8.2).
constexpr auto kKeyHistoryEnabled = "general/historyEnabled";
} // namespace

LibraryService::LibraryService(QObject *parent)
    : QObject(parent)
    , m_storage(std::make_unique<Storage>())
{
    if (m_storage->open()) {
        m_history = std::make_unique<HistoryStore>(m_storage.get());
        m_vocab = std::make_unique<VocabStore>(m_storage.get());
        m_ready = true;
    } else {
        qCWarning(w2tCore) << "LibraryService: storage unavailable; history/vocabulary disabled";
    }
}

LibraryService::~LibraryService() = default;

bool LibraryService::isReady() const
{
    return m_ready;
}

bool LibraryService::isRecordingPaused() const
{
    return Settings::instance().value(QString::fromLatin1(kKeyRecordingPaused), false).toBool();
}

void LibraryService::setRecordingPaused(bool paused)
{
    if (isRecordingPaused() == paused)
        return;
    Settings::instance().setValue(QString::fromLatin1(kKeyRecordingPaused), paused);
    qCInfo(w2tCore) << "history recording paused:" << paused;
    emit recordingPausedChanged(paused);
}

void LibraryService::recordHistory(
    const QString &trigger, const TranslateRequest &request, const TranslateResult &result)
{
    if (!m_ready || !result.ok())
        return;
    Settings &settings = Settings::instance();
    if (!settings.value(QString::fromLatin1(kKeyHistoryEnabled), true).toBool())
        return; // history disabled entirely
    if (isRecordingPaused())
        return; // privacy: temporary pause (DESIGN.md §8.2)

    HistoryRecord record;
    record.trigger = trigger;
    record.sourceText = request.text;
    record.sourceLang = result.detectedSourceLang.isEmpty() ? request.sourceLang : result.detectedSourceLang;
    record.targetLang = request.targetLang;
    record.engineId = result.engineId;
    record.resultText = result.translatedText;

    if (m_history->record(record) < 0)
        return;

    const int limit = settings.value(QString::fromLatin1(kKeyHistoryLimit), kDefaultHistoryLimit).toInt();
    m_history->enforceRetention(limit);
    emit historyChanged();
}

bool LibraryService::isSaved(const QString &word, const QString &lang) const
{
    return m_ready && m_vocab->contains(word, lang);
}

void LibraryService::addToVocabulary(const TranslateResult &result, const QString &fallbackWord, const QString &context)
{
    if (!m_ready)
        return;

    VocabRecord record;
    // Prefer the dictionary headword; fall back to the queried text.
    record.word = (result.dict.has_value() && !result.dict->word.isEmpty()) ? result.dict->word : fallbackWord;
    if (record.word.isEmpty())
        return;
    // The word's own language is the detected source (best available signal).
    record.lang
        = result.detectedSourceLang.isEmpty() ? LangDetector::detectLang(record.word) : result.detectedSourceLang;
    if (result.dict.has_value() && !result.dict->isEmpty()) {
        record.dict = result.dict;
        record.phonetic = result.dict->phonetic;
    }
    record.translation = result.translatedText;
    record.context = context;

    if (m_vocab->add(record) < 0)
        return;
    qCInfo(w2tCore) << "vocabulary +1" << record.word << record.lang;
    emit vocabularyChanged();
}

void LibraryService::removeFromVocabulary(const QString &word, const QString &lang)
{
    if (!m_ready)
        return;
    if (m_vocab->remove(word, lang)) {
        qCInfo(w2tCore) << "vocabulary -1" << word << lang;
        emit vocabularyChanged();
    }
}
