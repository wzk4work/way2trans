#include "AutostartManager.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>

// AutostartManager keeps the XDG autostart entry in lockstep with
// Settings::autostart() (DESIGN.md §10). The harness points XDG_CONFIG_HOME
// at the build tree, so these tests never touch the real user config.
class TestAutostartManager : public QObject {
    Q_OBJECT

private slots:
    void entryPathUnderXdgConfigHome();
    void constructorSyncsCurrentState();
    void toggleViaSettingsCreatesAndRemovesEntry();
    void entryContentsPointAtHiddenLaunch();
    void syncIsIdempotent();

private:
    QString readEntry() const;
};

QString TestAutostartManager::readEntry() const
{
    QFile file(AutostartManager::entryFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

void TestAutostartManager::entryPathUnderXdgConfigHome()
{
    const QString path = AutostartManager::entryFilePath();
    QVERIFY(path.endsWith(QStringLiteral("autostart/io.github.way2trans.desktop")));
}

void TestAutostartManager::constructorSyncsCurrentState()
{
    // Simulate a stale entry left behind while the setting is off: the
    // constructor's initial sync must remove it (config is source of truth).
    Settings::instance().setAutostart(false);
    QDir().mkpath(QFileInfo(AutostartManager::entryFilePath()).absolutePath());
    {
        QFile stale(AutostartManager::entryFilePath());
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("[Desktop Entry]\nExec=/moved/away\n");
    }

    AutostartManager manager;
    QVERIFY(!QFile::exists(AutostartManager::entryFilePath()));
}

void TestAutostartManager::toggleViaSettingsCreatesAndRemovesEntry()
{
    Settings::instance().setAutostart(false);
    AutostartManager manager;

    Settings::instance().setAutostart(true);
    QVERIFY(QFile::exists(AutostartManager::entryFilePath()));

    Settings::instance().setAutostart(false);
    QVERIFY(!QFile::exists(AutostartManager::entryFilePath()));
}

void TestAutostartManager::entryContentsPointAtHiddenLaunch()
{
    Settings::instance().setAutostart(false);
    AutostartManager manager;
    Settings::instance().setAutostart(true);

    const QString contents = readEntry();
    QVERIFY(contents.startsWith(QStringLiteral("[Desktop Entry]\n")));
    QVERIFY(contents.contains(QStringLiteral("Type=Application\n")));
    // The Exec line launches this binary silently.
    QVERIFY(contents.contains(QStringLiteral("Exec=%1\n").arg(AutostartManager::execLine())));
    QVERIFY(AutostartManager::execLine().endsWith(QStringLiteral(" --hidden")));
    QVERIFY(AutostartManager::execLine().contains(QCoreApplication::applicationFilePath()));

    Settings::instance().setAutostart(false);
}

void TestAutostartManager::syncIsIdempotent()
{
    Settings::instance().setAutostart(false);
    AutostartManager manager;

    QVERIFY(manager.sync(true));
    QVERIFY(manager.sync(true)); // rewrite over an existing entry
    QVERIFY(QFile::exists(AutostartManager::entryFilePath()));

    QVERIFY(manager.sync(false));
    QVERIFY(manager.sync(false)); // removing an absent entry is fine
    QVERIFY(!QFile::exists(AutostartManager::entryFilePath()));
}

QTEST_MAIN(TestAutostartManager)
#include "tst_autostartmanager.moc"
