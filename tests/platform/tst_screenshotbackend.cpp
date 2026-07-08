#include "PlatformFactory.h"
#include "Settings.h"
#include "screenshot/GrimScreenshotBackend.h"
#include "screenshot/PortalScreenshotBackend.h"
#include "screenshot/ScreenshotBackend.h"
#include "screenshot/X11ScreenshotBackend.h"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

// Screenshot backend selection + X11 grab. The factory-selection cases run
// under offscreen QPA (overrides isolate them from auto-detection). The actual
// grab assertion only runs on a real xcb session (QT_QPA_PLATFORM=xcb), where
// grabWindow returns real pixels; under offscreen it is skipped.
class TestScreenshotBackend : public QObject {
    Q_OBJECT

private slots:
    void init();
    void overrideSelectsX11();
    void overrideSelectsPortal();
    void overrideSelectsGrim();
    void probesDoNotCrash();
    void x11GrabProducesNonEmptyOnRealDisplay();

private:
    void setOverride(const QString &value);
};

void TestScreenshotBackend::init()
{
    setOverride(QString());
}

void TestScreenshotBackend::setOverride(const QString &value)
{
    Settings::instance().setValue(QStringLiteral("platform/screenshot"), value);
    Settings::instance().sync();
}

void TestScreenshotBackend::overrideSelectsX11()
{
    setOverride(QStringLiteral("x11"));
    QObject parent;
    ScreenshotBackend *b = PlatformFactory::createScreenshotBackend(&parent);
    QVERIFY(b);
    QCOMPARE(b->metaObject()->className(), "X11ScreenshotBackend");
}

void TestScreenshotBackend::overrideSelectsPortal()
{
    setOverride(QStringLiteral("portal"));
    QObject parent;
    ScreenshotBackend *b = PlatformFactory::createScreenshotBackend(&parent);
    QVERIFY(b);
    QCOMPARE(b->metaObject()->className(), "PortalScreenshotBackend");
}

void TestScreenshotBackend::overrideSelectsGrim()
{
    setOverride(QStringLiteral("grim"));
    QObject parent;
    ScreenshotBackend *b = PlatformFactory::createScreenshotBackend(&parent);
    QVERIFY(b);
    QCOMPARE(b->metaObject()->className(), "GrimScreenshotBackend");
}

void TestScreenshotBackend::probesDoNotCrash()
{
    // Availability probes must be side-effect free and never throw/crash,
    // whatever the session type.
    (void)PortalScreenshotBackend::isPortalAvailable();
    (void)GrimScreenshotBackend::isGrimAvailable();
    QVERIFY(true);
}

void TestScreenshotBackend::x11GrabProducesNonEmptyOnRealDisplay()
{
    if (!QGuiApplication::platformName().startsWith(QLatin1String("xcb")))
        QSKIP("not an xcb session; grabWindow needs a real X server");

    const QList<ScreenImage> shots = X11ScreenshotBackend::grabAllScreens();
    QVERIFY(!shots.isEmpty());
    for (const ScreenImage &shot : shots) {
        QVERIFY(!shot.image.isNull());
        QVERIFY(shot.image.width() > 0);
        QVERIFY(shot.image.height() > 0);
        QVERIFY(shot.geometry.isValid());
        QVERIFY(shot.dpr >= 1.0);
    }
}

QTEST_MAIN(TestScreenshotBackend)
#include "tst_screenshotbackend.moc"
