#include "MainWindow.h"
#include "SettingsPage.h"

#include <QListWidget>
#include <QStackedWidget>
#include <QTest>

// Smoke test for the MainWindow sidebar shell (DESIGN.md §7). Runs offscreen
// and with no services injected, so it exercises construction + the navigation
// layout without touching the network or a real database.
class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void hasFourPages();
    void showSettingsSwitchesPage();
    void constructsWithoutServices();
};

void TestMainWindow::hasFourPages()
{
    MainWindow window;
    auto *nav = window.findChild<QListWidget *>(QStringLiteral("mainNav"));
    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    QVERIFY(nav != nullptr);
    QVERIFY(pages != nullptr);
    // Navigation order is Translate / History / Vocabulary / Settings.
    QCOMPARE(nav->count(), 4);
    QCOMPARE(pages->count(), 4);
    for (int i = 0; i < nav->count(); ++i)
        QVERIFY(!nav->item(i)->text().isEmpty());
    // The window opens on the translate page.
    QCOMPARE(nav->currentRow(), 0);
    QCOMPARE(pages->currentIndex(), 0);
}

void TestMainWindow::showSettingsSwitchesPage()
{
    MainWindow window;
    QVERIFY(window.settingsPage() != nullptr);
    window.showSettings();
    auto *nav = window.findChild<QListWidget *>(QStringLiteral("mainNav"));
    QCOMPARE(nav->currentRow(), nav->count() - 1);
    // The settings page is the widget shown by the stack.
    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    QCOMPARE(pages->currentWidget(), window.settingsPage());
}

void TestMainWindow::constructsWithoutServices()
{
    // setServices(nullptr, nullptr) must be safe: empty models, no crash.
    MainWindow window;
    window.setServices(nullptr, nullptr);
    // translateText with no translate service is a no-op, not a crash.
    window.translateText(QStringLiteral("hello"));
    QVERIFY(true);
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
