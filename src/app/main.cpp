#include "Application.h"
#include "Logging.h"

#include <QStringList>

#include <cstdio>

int main(int argc, char **argv)
{
    Application app(argc, argv);

    const CliAction action = Application::parseCommandLine(app.arguments());
    if (action.kind == CliAction::Kind::Invalid) {
        qCWarning(w2tApp) << "Invalid command line:" << action.errorText;
        fprintf(stderr, "way2trans: %s\n", qUtf8Printable(action.errorText));
        return 2;
    }

    // Single-instance handshake (DESIGN.md §9): try to own the bus name
    // io.github.way2trans. If we get it, we are the primary instance and run
    // the event loop. If not, an instance is already running, so forward the
    // action to it over D-Bus and exit.
    if (app.tryBecomePrimary()) {
        return app.runAsPrimary(action);
    }

    if (Application::forwardToRunningInstance(action)) {
        qCInfo(w2tApp) << "Action forwarded to running instance; exiting";
        return 0;
    }

    fprintf(stderr, "way2trans: could not reach running instance over D-Bus\n");
    return 1;
}
