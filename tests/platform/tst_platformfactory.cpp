#include "HotkeyBackend.h"
#include "PlatformFactory.h"
#include "PopupPlacement.h"
#include "SelectionSource.h"
#include "Settings.h"

#include <QTest>

// Tests the PlatformFactory backend-selection decisions that are driven by the
// [platform] override keys. These run under QT_QPA_PLATFORM=offscreen, so
// QGuiApplication::platformName() is neither "xcb" nor "wayland"; that isolates
// the override branches from the auto-detection branches (which depend on a real
// session type we can't fake here). The Wayland auto path is verified later on a
// real KDE/Sway box; see the M2 test checklist in the PR.
class TestPlatformFactory : public QObject {
    Q_OBJECT

private slots:
    void init();

    void selectionOverrideX11();
    void selectionOverrideWayland();
    void hotkeyOverrideNull();
    void popupOverridePlain();
    void popupOverrideLayerShellFallsBackWhenNotWayland();

private:
    void setOverride(const QString &cap, const QString &value);
};

void TestPlatformFactory::init()
{
    // Clear any leftover overrides between tests.
    setOverride(QStringLiteral("selection"), QString());
    setOverride(QStringLiteral("hotkey"), QString());
    setOverride(QStringLiteral("popup"), QString());
}

void TestPlatformFactory::setOverride(const QString &cap, const QString &value)
{
    Settings::instance().setValue(QStringLiteral("platform/%1").arg(cap), value);
    Settings::instance().sync();
}

void TestPlatformFactory::selectionOverrideX11()
{
    setOverride(QStringLiteral("selection"), QStringLiteral("x11"));
    QObject parent;
    SelectionSource *src = PlatformFactory::createSelectionSource(&parent);
    QVERIFY(src);
    QCOMPARE(src->metaObject()->className(), "X11SelectionSource");
}

void TestPlatformFactory::selectionOverrideWayland()
{
    setOverride(QStringLiteral("selection"), QStringLiteral("wl-paste"));
    QObject parent;
    SelectionSource *src = PlatformFactory::createSelectionSource(&parent);
    QVERIFY(src);
    QCOMPARE(src->metaObject()->className(), "WlSelectionSource");
}

void TestPlatformFactory::hotkeyOverrideNull()
{
    setOverride(QStringLiteral("hotkey"), QStringLiteral("null"));
    QObject parent;
    HotkeyBackend *hk = PlatformFactory::createHotkeyBackend(&parent);
    QVERIFY(hk);
    QCOMPARE(hk->name(), QStringLiteral("null"));
    // Null backend never grabs a key -> registerAction returns false so the
    // caller shows manual-binding guidance.
    QCOMPARE(hk->registerAction(QStringLiteral("translate-selection"), QKeySequence(QStringLiteral("Ctrl+Alt+D")),
                 QStringLiteral("Translate selection")),
        false);
}

void TestPlatformFactory::popupOverridePlain()
{
    setOverride(QStringLiteral("popup"), QStringLiteral("plain"));
    auto placement = PlatformFactory::createPopupPlacement();
    QVERIFY(placement);
    QCOMPARE(placement->name(), QStringLiteral("plain"));
    QCOMPARE(placement->supportsCursorPlacement(), false);
}

void TestPlatformFactory::popupOverrideLayerShellFallsBackWhenNotWayland()
{
    // Requesting layer-shell off a Wayland session: even when LayerShellQt is
    // compiled in, the factory only builds it on a real Wayland QPA. Under
    // offscreen it must fall back rather than crash, and never claim cursor
    // placement (Wayland limitation).
    setOverride(QStringLiteral("popup"), QStringLiteral("layer-shell"));
    auto placement = PlatformFactory::createPopupPlacement();
    QVERIFY(placement);
    QCOMPARE(placement->supportsCursorPlacement(), false);
}

QTEST_MAIN(TestPlatformFactory)
#include "tst_platformfactory.moc"
