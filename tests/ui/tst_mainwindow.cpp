#include "MainWindow.h"

#include <QTabWidget>
#include <QTest>

// Smoke test for the MainWindow tabbed shell (DESIGN.md §7). Runs offscreen and
// with no services injected, so it exercises construction + the tab layout
// without touching the network or a real database.
class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void hasThreeTabs();
    void constructsWithoutServices();
};

void TestMainWindow::hasThreeTabs()
{
    MainWindow window;
    auto *tabs = window.findChild<QTabWidget *>();
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 3);
    // Tab order is Translate / History / Vocabulary.
    QVERIFY(!tabs->tabText(0).isEmpty());
    QVERIFY(!tabs->tabText(1).isEmpty());
    QVERIFY(!tabs->tabText(2).isEmpty());
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
