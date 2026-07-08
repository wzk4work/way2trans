#include "storage/HistoryStore.h"
#include "storage/Storage.h"
#include "storage/VocabStore.h"

#include <QDateTime>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <vector>

// Storage-layer tests (DESIGN.md §8). Each case builds a throwaway database in a
// QTemporaryDir so nothing touches the developer's real ~/.local/share data.
class TestStorage : public QObject {
    Q_OBJECT

private slots:
    void opensAndMigrates();
    void historyDedupesWithinWindow();
    void historyRetentionKeepsFavorites();
    void historyFtsSearch();
    void vocabConflictBumpsLookupCount();
    void vocabSortAndStatusFilter();
    void exportAnkiTsvFields();
    void exportCsvQuoting();
    void jsonRoundTripAndMerge();

private:
    // Build a fresh open Storage in a per-test temp dir. The QTemporaryDir is
    // parked in m_dirs so it outlives the returned Storage.
    std::unique_ptr<Storage> freshDb()
    {
        auto dir = std::make_unique<QTemporaryDir>();
        Q_ASSERT(dir->isValid());
        const QString path = dir->filePath(QStringLiteral("test.db"));
        m_dirs.push_back(std::move(dir));
        auto storage = std::make_unique<Storage>();
        const bool ok = storage->open(path);
        Q_ASSERT(ok);
        return storage;
    }

    std::vector<std::unique_ptr<QTemporaryDir>> m_dirs;
};

void TestStorage::opensAndMigrates()
{
    auto db = freshDb();
    QVERIFY(db->isOpen());
    QCOMPARE(db->schemaVersion(), Storage::latestSchemaVersion());

    // Re-opening the same file is idempotent (already at latest version).
    const QString path = db->databasePath();
    Storage again;
    QVERIFY(again.open(path));
    QCOMPARE(again.schemaVersion(), Storage::latestSchemaVersion());
}

void TestStorage::historyDedupesWithinWindow()
{
    auto db = freshDb();
    HistoryStore history(db.get());
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    HistoryRecord r;
    r.trigger = QStringLiteral("selection");
    r.sourceText = QStringLiteral("hello world");
    r.resultText = QStringLiteral("你好世界");
    r.createdAt = now;
    const qint64 id1 = history.record(r, 600);
    QVERIFY(id1 > 0);

    // Same text a minute later -> updates in place, no new row.
    r.createdAt = now + 60;
    r.resultText = QStringLiteral("你好，世界");
    const qint64 id2 = history.record(r, 600);
    QCOMPARE(id2, id1);
    QCOMPARE(history.count(), 1);

    // Same text well outside the window -> a new row.
    r.createdAt = now + 5000;
    const qint64 id3 = history.record(r, 600);
    QVERIFY(id3 != id1);
    QCOMPARE(history.count(), 2);
}

void TestStorage::historyRetentionKeepsFavorites()
{
    auto db = freshDb();
    HistoryStore history(db.get());
    const qint64 base = QDateTime::currentSecsSinceEpoch();

    // 5 rows with distinct text and increasing timestamps.
    QList<qint64> ids;
    for (int i = 0; i < 5; ++i) {
        HistoryRecord r;
        r.trigger = QStringLiteral("input");
        r.sourceText = QStringLiteral("entry %1").arg(i);
        r.resultText = QStringLiteral("result %1").arg(i);
        r.createdAt = base + i;
        ids << history.record(r);
    }
    QCOMPARE(history.count(), 5);

    // Favorite the oldest so it must survive trimming.
    QVERIFY(history.setFavorite(ids.first(), true));

    // 4 non-favorites; keep 2 -> the 2 oldest non-favorites are dropped. The
    // favorite is exempt from the cap, so 3 rows remain (2 non-fav + 1 fav).
    const int deleted = history.enforceRetention(2);
    QCOMPARE(deleted, 2);
    QCOMPARE(history.count(), 3);

    // The favorite (oldest) is still present.
    const auto recent = history.recent(100);
    bool favPresent = false;
    for (const HistoryRecord &r : recent)
        if (r.id == ids.first())
            favPresent = true;
    QVERIFY(favPresent);

    // maxRows <= 0 disables trimming.
    QCOMPARE(history.enforceRetention(0), 0);
}

void TestStorage::historyFtsSearch()
{
    auto db = freshDb();
    HistoryStore history(db.get());

    HistoryRecord a;
    a.trigger = QStringLiteral("input");
    a.sourceText = QStringLiteral("the quick brown fox");
    a.resultText = QStringLiteral("敏捷的棕色狐狸");
    history.record(a);

    HistoryRecord b;
    b.trigger = QStringLiteral("input");
    b.sourceText = QStringLiteral("lazy dog sleeps");
    b.resultText = QStringLiteral("懒狗睡觉");
    history.record(b);

    const auto hits = history.search(QStringLiteral("fox"));
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().sourceText, QStringLiteral("the quick brown fox"));

    // No matches -> empty.
    QVERIFY(history.search(QStringLiteral("elephant")).isEmpty());

    // Punctuation/FTS operators in the query must not blow up the parser: each
    // token is quoted, so "dog" matches literally and stray quotes are harmless.
    const auto safe = history.search(QStringLiteral("dog\""));
    QCOMPARE(safe.size(), 1);
    // A query full of FTS operator characters returns cleanly (no results, no crash).
    QVERIFY(history.search(QStringLiteral("* OR (")).isEmpty());

    // Deleting keeps the FTS index consistent (trigger-maintained).
    QVERIFY(history.remove(hits.first().id));
    QVERIFY(history.search(QStringLiteral("fox")).isEmpty());
}

void TestStorage::vocabConflictBumpsLookupCount()
{
    auto db = freshDb();
    VocabStore vocab(db.get());

    VocabRecord r;
    r.word = QStringLiteral("book");
    r.lang = QStringLiteral("en");
    r.translation = QStringLiteral("书");
    const qint64 id1 = vocab.add(r);
    QVERIFY(id1 > 0);
    QVERIFY(vocab.contains(QStringLiteral("book"), QStringLiteral("en")));

    // Same (word,lang) again -> same row, lookup_count incremented.
    const qint64 id2 = vocab.add(r);
    QCOMPARE(id2, id1);
    QCOMPARE(vocab.count(), 1);
    auto found = vocab.find(QStringLiteral("book"), QStringLiteral("en"));
    QVERIFY(found.has_value());
    QCOMPARE(found->lookupCount, 2);

    // Different lang is a distinct entry.
    r.lang = QStringLiteral("de");
    vocab.add(r);
    QCOMPARE(vocab.count(), 2);
}

void TestStorage::vocabSortAndStatusFilter()
{
    auto db = freshDb();
    VocabStore vocab(db.get());

    VocabRecord a;
    a.word = QStringLiteral("apple");
    a.lang = QStringLiteral("en");
    const qint64 aid = vocab.add(a);

    VocabRecord b;
    b.word = QStringLiteral("banana");
    b.lang = QStringLiteral("en");
    vocab.add(b);
    // Look up "banana" twice more so it outranks apple by lookup_count.
    vocab.add(b);
    vocab.add(b);

    const auto byCount = vocab.list(VocabStore::Sort::LookupCountDesc);
    QCOMPARE(byCount.size(), 2);
    QCOMPARE(byCount.first().word, QStringLiteral("banana"));

    // Status flow + filter.
    QVERIFY(vocab.setStatus(aid, 2)); // mastered
    const auto mastered = vocab.list(VocabStore::Sort::AddedAtDesc, 2);
    QCOMPARE(mastered.size(), 1);
    QCOMPARE(mastered.first().word, QStringLiteral("apple"));
}

void TestStorage::exportAnkiTsvFields()
{
    auto db = freshDb();
    VocabStore vocab(db.get());

    DictEntry dict;
    dict.word = QStringLiteral("run");
    dict.phonetic = QStringLiteral("rʌn");
    DictMeaning verb;
    verb.partOfSpeech = QStringLiteral("verb");
    verb.definitions = {QStringLiteral("跑"), QStringLiteral("运行")};
    dict.meanings << verb;

    VocabRecord r;
    r.word = QStringLiteral("run");
    r.lang = QStringLiteral("en");
    r.phonetic = QStringLiteral("rʌn");
    r.dict = dict;
    r.context = QStringLiteral("I run every day");
    r.note = QStringLiteral("common");
    vocab.add(r);

    const QString tsv = vocab.exportAnkiTsv();
    const QStringList cols = tsv.split(QLatin1Char('\t'));
    QCOMPARE(cols.size(), 5);
    QCOMPARE(cols.at(0), QStringLiteral("run"));
    QCOMPARE(cols.at(1), QStringLiteral("rʌn"));
    QCOMPARE(cols.at(2), QStringLiteral("verb 跑, 运行"));
    QCOMPARE(cols.at(3), QStringLiteral("I run every day"));
    QCOMPARE(cols.at(4), QStringLiteral("common"));
}

void TestStorage::exportCsvQuoting()
{
    auto db = freshDb();
    VocabStore vocab(db.get());

    VocabRecord r;
    r.word = QStringLiteral("hello, world");
    r.lang = QStringLiteral("en");
    r.translation = QStringLiteral("she said \"hi\"");
    vocab.add(r);

    const QString csv = vocab.exportCsv();
    // Header + one data line + trailing newline.
    const QStringList lines = csv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines.at(0).startsWith(QStringLiteral("word,lang")));
    // Comma-bearing field is quoted; embedded quotes are doubled.
    QVERIFY(lines.at(1).contains(QStringLiteral("\"hello, world\"")));
    QVERIFY(lines.at(1).contains(QStringLiteral("\"she said \"\"hi\"\"\"")));
}

void TestStorage::jsonRoundTripAndMerge()
{
    auto src = freshDb();
    VocabStore srcVocab(src.get());

    DictEntry dict;
    dict.word = QStringLiteral("study");
    DictMeaning m;
    m.partOfSpeech = QStringLiteral("verb");
    m.definitions = {QStringLiteral("学习")};
    dict.meanings << m;

    VocabRecord r;
    r.word = QStringLiteral("study");
    r.lang = QStringLiteral("en");
    r.dict = dict;
    r.tags = {QStringLiteral("school"), QStringLiteral("verb")};
    r.status = 1;
    r.lookupCount = 5;
    srcVocab.add(r);

    const QString json = srcVocab.exportJson();

    // Import into a fresh DB: full fidelity round-trip.
    auto dst = freshDb();
    VocabStore dstVocab(dst.get());
    QCOMPARE(dstVocab.importJson(json), 1);
    auto got = dstVocab.find(QStringLiteral("study"), QStringLiteral("en"));
    QVERIFY(got.has_value());
    QCOMPARE(got->lookupCount, 5);
    QCOMPARE(got->status, 1);
    QVERIFY(got->dict.has_value());
    QCOMPARE(got->dict->meanings.first().definitions.first(), QStringLiteral("学习"));
    QCOMPARE(got->tags, (QStringList {QStringLiteral("school"), QStringLiteral("verb")}));

    // Merge conflict: a lower lookup_count must NOT overwrite; a higher one wins.
    const QString lower = json; // lookup_count 5
    QCOMPARE(dstVocab.importJson(lower), 0); // not higher -> no change
    QCOMPARE(dstVocab.find(QStringLiteral("study"), QStringLiteral("en"))->lookupCount, 5);

    QString higher = json;
    higher.replace(QStringLiteral("\"lookup_count\": 5"), QStringLiteral("\"lookup_count\": 9"));
    QCOMPARE(dstVocab.importJson(higher), 1);
    QCOMPARE(dstVocab.find(QStringLiteral("study"), QStringLiteral("en"))->lookupCount, 9);
}

QTEST_MAIN(TestStorage)
#include "tst_storage.moc"
