#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>

class Application;

namespace w2t::dbus {
// Single source of truth for the D-Bus names (DESIGN.md §9, AGENTS.md).
inline constexpr auto kServiceName = WAY2TRANS_APP_ID; // io.github.way2trans
inline constexpr auto kObjectPath = "/io/github/way2trans";
inline constexpr auto kInterfaceName = WAY2TRANS_APP_ID;
} // namespace w2t::dbus

// D-Bus adaptor exposing the way2trans control surface on
// /io/github/way2trans (interface io.github.way2trans).
//
// The adaptor is a thin shell: every method delegates to the owning
// Application, which holds the actual UI/lifecycle state. Using
// QDBusAbstractAdaptor gives us automatic introspection XML from the moc
// scan of the public slots below.
class DBusService : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", WAY2TRANS_APP_ID)

public:
    explicit DBusService(Application *app);

public slots:
    void TriggerSelection();
    void TriggerCapture();
    void TriggerCaptureOcr();
    void TriggerClipboard();
    void ShowMain();
    QString Translate(const QString &text);

private:
    Application *m_app;
};
