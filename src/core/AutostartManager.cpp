#include "AutostartManager.h"

#include "Logging.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// Quote one Exec argument per the desktop-entry spec: double quotes, with the
// characters the spec reserves inside quotes backslash-escaped. Plain paths
// stay unquoted for readability.
QString quoteExecArgument(const QString &argument)
{
    static const QString needsQuoting = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
    bool plain = !argument.isEmpty();
    for (const QChar c : argument) {
        if (needsQuoting.contains(c)) {
            plain = false;
            break;
        }
    }
    if (plain)
        return argument;

    QString quoted = argument;
    for (const char *ch : {"\\", "\"", "`", "$"}) {
        quoted.replace(QLatin1String(ch), QLatin1Char('\\') + QLatin1String(ch));
    }
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

} // namespace

AutostartManager::AutostartManager(QObject *parent)
    : QObject(parent)
{
    connect(&Settings::instance(), &Settings::autostartChanged, this, &AutostartManager::sync);
    // Initial sync: the config is the source of truth, and rewriting the
    // entry heals an Exec line pointing at a moved binary.
    sync(Settings::instance().autostart());
}

QString AutostartManager::entryFilePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(base).filePath(
        QStringLiteral("autostart/") + QLatin1String(WAY2TRANS_APP_ID) + QStringLiteral(".desktop"));
}

QString AutostartManager::execLine()
{
    // Inside an AppImage applicationFilePath() points at the transient mount;
    // $APPIMAGE carries the persistent path of the image itself.
    QString executable = qEnvironmentVariable("APPIMAGE");
    if (executable.isEmpty())
        executable = QCoreApplication::applicationFilePath();
    return quoteExecArgument(executable) + QStringLiteral(" --hidden");
}

QString AutostartManager::desktopEntryContents()
{
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=way2trans\n"
                          "Comment=Selection and screenshot translation tool\n"
                          "Exec=%1\n"
                          "Terminal=false\n"
                          "X-GNOME-Autostart-enabled=true\n")
        .arg(execLine());
}

bool AutostartManager::sync(bool enabled)
{
    const QString path = entryFilePath();

    if (!enabled) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            qCWarning(w2tCore) << "Could not remove autostart entry" << path;
            return false;
        }
        return true;
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qCWarning(w2tCore) << "Could not create autostart directory for" << path;
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(w2tCore) << "Could not open autostart entry for writing:" << path << file.errorString();
        return false;
    }
    file.write(desktopEntryContents().toUtf8());
    if (!file.commit()) {
        qCWarning(w2tCore) << "Could not write autostart entry:" << path << file.errorString();
        return false;
    }
    qCInfo(w2tCore) << "Autostart entry written to" << path;
    return true;
}
