#include "Application.h"

#include "DBusService.h"
#include "Logging.h"
#include "MainWindow.h"
#include "TrayIcon.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QStringList>

#include <cstdio>

namespace {
// Valid `trigger` sub-verbs and their D-Bus method names.
QString dbusMethodForTrigger(const QString &trigger)
{
    if (trigger == QLatin1String("selection"))
        return QStringLiteral("TriggerSelection");
    if (trigger == QLatin1String("capture"))
        return QStringLiteral("TriggerCapture");
    if (trigger == QLatin1String("clipboard"))
        return QStringLiteral("TriggerClipboard");
    if (trigger == QLatin1String("show-main"))
        return QStringLiteral("ShowMain");
    return {};
}
} // namespace

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    setApplicationName(QStringLiteral("way2trans"));
    setApplicationDisplayName(QStringLiteral("way2trans"));
    setApplicationVersion(QStringLiteral("0.1.0"));
    setOrganizationName(QStringLiteral("way2trans"));
    setDesktopFileName(QStringLiteral(WAY2TRANS_APP_ID));

    // The tray/window own the process lifetime; closing the main window must
    // not quit the daemon.
    setQuitOnLastWindowClosed(false);
}

Application::~Application() = default;

CliAction Application::parseCommandLine(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("way2trans - Linux word/screenshot translation tool"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption textOption(QStringList{QStringLiteral("text")},
        QStringLiteral("Translate the given text and print the result."),
        QStringLiteral("text"));
    parser.addOption(textOption);

    // Positional: `trigger <action>`.
    parser.addPositionalArgument(QStringLiteral("command"),
        QStringLiteral("trigger <selection|capture|clipboard|show-main>"));

    if (!parser.parse(arguments)) {
        return CliAction{CliAction::Kind::Invalid, {}, {}, parser.errorText()};
    }

    // Honour the built-in --help / --version options: print and exit cleanly
    // instead of treating them as an actionable command.
    if (parser.isSet(QStringLiteral("help")))
        parser.showHelp(0); // does not return
    if (parser.isSet(QStringLiteral("version")))
        parser.showVersion(); // does not return

    if (parser.isSet(textOption)) {
        return CliAction{CliAction::Kind::TranslateText, {}, parser.value(textOption), {}};
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        return CliAction{CliAction::Kind::RunDaemon, {}, {}, {}};
    }

    const QString command = positional.first();
    if (command == QLatin1String("trigger")) {
        if (positional.size() < 2) {
            return CliAction{CliAction::Kind::Invalid, {}, {},
                QStringLiteral("'trigger' requires an action: selection|capture|clipboard|show-main")};
        }
        const QString action = positional.at(1);
        if (dbusMethodForTrigger(action).isEmpty()) {
            return CliAction{CliAction::Kind::Invalid, {}, {},
                QStringLiteral("unknown trigger action: %1").arg(action)};
        }
        return CliAction{CliAction::Kind::Trigger, action, {}, {}};
    }

    return CliAction{CliAction::Kind::Invalid, {}, {},
        QStringLiteral("unknown command: %1").arg(command)};
}

bool Application::registerDBusService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(w2tApp) << "Session bus not available; running without single-instance/D-Bus";
        return false;
    }

    // Publish the object first, then claim the well-known name. If the name is
    // already owned, another instance is running.
    // Parented to this; Qt owns it. We only null the pointer back out on failure.
    m_dbusAdaptor = new DBusService(this);
    if (!bus.registerObject(QString::fromLatin1(w2t::dbus::kObjectPath), this,
            QDBusConnection::ExportAdaptors)) {
        qCWarning(w2tApp) << "Failed to register D-Bus object at" << w2t::dbus::kObjectPath;
        delete m_dbusAdaptor;
        m_dbusAdaptor = nullptr;
        return false;
    }

    if (!bus.registerService(QString::fromLatin1(w2t::dbus::kServiceName))) {
        // Name taken -> another instance owns it.
        bus.unregisterObject(QString::fromLatin1(w2t::dbus::kObjectPath));
        delete m_dbusAdaptor;
        m_dbusAdaptor = nullptr;
        return false;
    }

    qCInfo(w2tApp) << "Registered D-Bus service" << w2t::dbus::kServiceName
                   << "at" << w2t::dbus::kObjectPath;
    return true;
}

bool Application::tryBecomePrimary()
{
    return registerDBusService();
}

int Application::runAsPrimary(const CliAction &initialAction)
{
    Q_ASSERT(m_dbusAdaptor); // tryBecomePrimary() must have succeeded first.

    m_mainWindow = std::make_unique<MainWindow>();

    m_tray = std::make_unique<TrayIcon>(this);
    if (m_tray->isAvailable()) {
        m_tray->show();
    } else {
        qCInfo(w2tApp) << "No system tray available; running without a tray icon";
    }

    // Perform the action this invocation asked for.
    dispatch(initialAction);

    return exec();
}

bool Application::forwardToRunningInstance(const CliAction &action)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(w2tApp) << "Cannot forward action: session bus not available";
        return false;
    }

    QDBusInterface iface(QString::fromLatin1(w2t::dbus::kServiceName),
        QString::fromLatin1(w2t::dbus::kObjectPath),
        QString::fromLatin1(w2t::dbus::kInterfaceName), bus);
    if (!iface.isValid()) {
        qCWarning(w2tApp) << "Cannot forward action: running instance interface invalid:"
                          << iface.lastError().message();
        return false;
    }

    if (action.kind == CliAction::Kind::TranslateText) {
        QDBusReply<QString> reply = iface.call(QStringLiteral("Translate"), action.text);
        if (!reply.isValid()) {
            qCWarning(w2tApp) << "Translate forward failed:" << reply.error().message();
            return false;
        }
        // Print the running instance's result to stdout for scripting.
        fprintf(stdout, "%s\n", qUtf8Printable(reply.value()));
        return true;
    }

    const QString method = dbusMethodForTrigger(
        action.kind == CliAction::Kind::Trigger ? action.trigger : QStringLiteral("show-main"));
    QDBusReply<void> reply = iface.call(method);
    if (!reply.isValid()) {
        qCWarning(w2tApp) << "Forward of" << method << "failed:" << reply.error().message();
        return false;
    }
    qCInfo(w2tApp) << "Forwarded" << method << "to running instance";
    return true;
}

void Application::dispatch(const CliAction &action)
{
    switch (action.kind) {
    case CliAction::Kind::Trigger: {
        if (action.trigger == QLatin1String("selection"))
            triggerSelection();
        else if (action.trigger == QLatin1String("capture"))
            triggerCapture();
        else if (action.trigger == QLatin1String("clipboard"))
            triggerClipboard();
        else if (action.trigger == QLatin1String("show-main"))
            showMainWindow();
        break;
    }
    case CliAction::Kind::TranslateText:
        fprintf(stdout, "%s\n", qUtf8Printable(translate(action.text)));
        break;
    case CliAction::Kind::RunDaemon:
        // Bare launch: just sit resident. Show the window so first-run users
        // see something; later milestones may make this configurable.
        showMainWindow();
        break;
    case CliAction::Kind::Invalid:
        qCWarning(w2tApp) << "Ignoring invalid action:" << action.errorText;
        break;
    }
}

// --- Actions ---------------------------------------------------------------

void Application::triggerSelection()
{
    // M0: no platform selection backend yet (arrives in M1). Log the intent.
    qCInfo(w2tApp) << "triggerSelection (M0 stub)";
}

void Application::triggerCapture()
{
    qCInfo(w2tApp) << "triggerCapture (M0 stub)";
}

void Application::triggerClipboard()
{
    qCInfo(w2tApp) << "triggerClipboard (M0 stub)";
}

void Application::showMainWindow()
{
    qCInfo(w2tApp) << "showMainWindow";
    if (!m_mainWindow) {
        // Can happen if a stray D-Bus call arrives before runAsPrimary built it.
        m_mainWindow = std::make_unique<MainWindow>();
    }
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
}

void Application::openSettings()
{
    // M0: settings dialog is a later milestone.
    qCInfo(w2tApp) << "openSettings (M0 stub)";
}

QString Application::translate(const QString &text)
{
    // M0: no translation engines yet (M1). Return a placeholder.
    qCInfo(w2tApp) << "translate (M0 stub) for text of length" << text.size();
    return QStringLiteral("[way2trans placeholder] %1").arg(text);
}
