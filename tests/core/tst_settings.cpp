#include "Settings.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>

// Minimal but real M0 test: exercise the Settings singleton's typed accessors,
// the change signals, and a persisted read/write round-trip on disk.
class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void configPathUnderXdgConfigHome();
    void defaultsAreReturned();
    void setterEmitsSignalAndPersists();
    void genericValueRoundTrip();
};

void TestSettings::configPathUnderXdgConfigHome()
{
    // The test harness sets XDG_CONFIG_HOME to a build-tree dir; the config
    // file must live under it (DESIGN.md §10 path shape).
    const QString path = Settings::configFilePath();
    QVERIFY(path.endsWith(QStringLiteral("way2trans/way2trans.conf")));
}

void TestSettings::defaultsAreReturned()
{
    Settings &s = Settings::instance();
    QCOMPARE(s.sourceLangRule(), QStringLiteral("auto"));
    QCOMPARE(s.targetLang(), QStringLiteral("en"));
    QCOMPARE(s.autostart(), false);
}

void TestSettings::setterEmitsSignalAndPersists()
{
    Settings &s = Settings::instance();

    QSignalSpy spy(&s, &Settings::targetLangChanged);
    s.setTargetLang(QStringLiteral("zh-CN"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("zh-CN"));
    QCOMPARE(s.targetLang(), QStringLiteral("zh-CN"));

    // Setting the same value again must not re-emit.
    s.setTargetLang(QStringLiteral("zh-CN"));
    QCOMPARE(spy.count(), 0);

    // Flush and confirm it actually hit disk.
    s.sync();
    QVERIFY(QFile::exists(Settings::configFilePath()));
}

void TestSettings::genericValueRoundTrip()
{
    Settings &s = Settings::instance();
    s.setValue(QStringLiteral("engines/google/enabled"), true);
    s.sync();
    QCOMPARE(s.value(QStringLiteral("engines/google/enabled")).toBool(), true);
}

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
