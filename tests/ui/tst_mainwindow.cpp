#include "MainWindow.h"
#include "ServicesPage.h"

#include <QListWidget>
#include <QStackedWidget>
#include <QTest>

// Smoke test for the MainWindow sidebar shell (DESIGN.md §7). Runs offscreen
// and with no services injected, so it exercises construction + the navigation
// layout without touching the network or a real database.
class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void hasFlatGroupedNavigation();
    void showSettingsSwitchesPage();
    void constructsWithoutServices();
};

void TestMainWindow::hasFlatGroupedNavigation()
{
    MainWindow window;
    auto *nav = window.findChild<QListWidget *>(QStringLiteral("mainNav"));
    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    QVERIFY(nav != nullptr);
    QVERIFY(pages != nullptr);
    // Translate / History / Vocabulary + "Settings" group header + General /
    // Hotkeys / Services / OCR / Platform: nine nav rows over eight pages.
    QCOMPARE(nav->count(), 9);
    QCOMPARE(pages->count(), 8);

    // Exactly one non-selectable group header.
    int headers = 0;
    for (int i = 0; i < nav->count(); ++i) {
        if (!(nav->item(i)->flags() & Qt::ItemIsSelectable))
            ++headers;
        QVERIFY(!nav->item(i)->text().isEmpty());
    }
    QCOMPARE(headers, 1);

    // The window opens on the translate page.
    QCOMPARE(nav->currentRow(), 0);
    QCOMPARE(pages->currentIndex(), 0);
}

void TestMainWindow::showSettingsSwitchesPage()
{
    MainWindow window;
    QVERIFY(window.servicesPage() != nullptr);
    window.showSettings();
    auto *nav = window.findChild<QListWidget *>(QStringLiteral("mainNav"));
    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    // showSettings() lands on the first settings entry (the row after the
    // group header) and the stack follows the item's page index.
    QVERIFY(nav->currentItem() != nullptr);
    QCOMPARE(pages->currentIndex(), nav->currentItem()->data(Qt::UserRole).toInt());
    QVERIFY(pages->currentIndex() >= 3);
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
