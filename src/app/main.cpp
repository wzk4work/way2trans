#include "Application.h"
#include "Logging.h"

#include <QLocale>
#include <QStringList>
#include <QTranslator>

#include <cstdio>

int main(int argc, char **argv)
{
    Application app(argc, argv);

#ifdef WAY2TRANS_HAVE_TRANSLATIONS
    // Load UI translations for the system locale before any widgets are built.
    // Falls back to the English source strings when no matching .qm exists.
    auto *translator = new QTranslator(&app);
    if (translator->load(QLocale(), QStringLiteral("way2trans"), QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(translator);
    } else {
        qCInfo(w2tApp) << "No translation for locale" << QLocale().name() << "; using source strings";
    }
#endif

    const CliAction action = Application::parseCommandLine(app.arguments());
    if (action.kind == CliAction::Kind::Invalid) {
        qCWarning(w2tApp) << "Invalid command line:" << action.errorText;
        fprintf(stderr, "way2trans: %s\n", qUtf8Printable(action.errorText));
        return 2;
    }

    // Headless OCR+translate verification entry (DESIGN.md §13 M3): runs
    // standalone with its own local event loop, never touching the daemon /
    // single-instance handshake or the tray.
    if (action.kind == CliAction::Kind::OcrTest) {
        return app.runOcrTest(action.imagePath);
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
