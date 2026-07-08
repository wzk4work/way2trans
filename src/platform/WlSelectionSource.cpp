#include "WlSelectionSource.h"

#include "Logging.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

namespace {
const QString kWlPaste = QStringLiteral("wl-paste");
} // namespace

WlSelectionSource::WlSelectionSource(QObject *parent)
    : SelectionSource(parent)
{ }

bool WlSelectionSource::isWlPasteAvailable()
{
    return !QStandardPaths::findExecutable(kWlPaste).isEmpty();
}

QString WlSelectionSource::interpretOutput(const QByteArray &stdoutBytes, bool exitOk)
{
    // wl-paste exits non-zero when there is no selection to offer; that is a
    // normal empty selection, not a failure. When it succeeds, the payload is
    // the selection text. --no-newline already strips a single trailing newline;
    // we trim defensively so callers see a clean empty-string for whitespace.
    if (!exitOk)
        return QString();
    return QString::fromUtf8(stdoutBytes).trimmed();
}

void WlSelectionSource::fetchSelection()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        qCDebug(w2tPlatform) << "WlSelectionSource: fetch already in flight; ignoring";
        return;
    }

    if (!isWlPasteAvailable()) {
        qCWarning(w2tPlatform) << "WlSelectionSource: wl-paste not found on PATH";
        emit fetchFailed(tr("wl-paste not found. Install wl-clipboard to read the "
                            "selection on Wayland."));
        return;
    }

    // One QProcess per fetch, parented so it is cleaned up with this object even
    // if a fetch is still running at shutdown.
    m_process = new QProcess(this);

    // Emit exactly once regardless of which terminating signal fires first.
    auto guard = std::make_shared<bool>(false);
    QProcess *proc = m_process;

    auto finishEmpty = [this, guard](const QString &reason) {
        if (*guard)
            return;
        *guard = true;
        qCWarning(w2tPlatform) << "WlSelectionSource:" << reason;
        // A failure to run the tool is a real error (missing/broken wl-clipboard);
        // callers surface the install hint. Compare with a clean non-zero exit,
        // handled in the finished path as an empty selection.
        emit fetchFailed(tr("Could not run wl-paste (%1). Is wl-clipboard "
                            "installed?")
                             .arg(reason));
    };

    connect(proc, &QProcess::errorOccurred, this, [finishEmpty](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishEmpty(QStringLiteral("failed to start"));
    });

    connect(proc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
        [this, guard, proc](int exitCode, QProcess::ExitStatus status) {
            if (*guard)
                return;
            *guard = true;
            const bool exitOk = status == QProcess::NormalExit && exitCode == 0;
            const QString text = interpretOutput(proc->readAllStandardOutput(), exitOk);
            qCDebug(w2tPlatform) << "WlSelectionSource: wl-paste exit" << exitCode << "status" << status
                                 << "selection length" << text.size();
            emit selectionFetched(text);
        });

    // Timeout guard: if the compositor never answers, kill and treat as empty.
    QTimer::singleShot(kTimeoutMs, proc, [proc, guard]() {
        if (*guard || proc->state() == QProcess::NotRunning)
            return;
        qCWarning(w2tPlatform) << "WlSelectionSource: wl-paste timed out; killing";
        proc->kill(); // triggers finished() with a crash status -> empty selection
    });

    // Reap the process object after it terminates without blocking the caller.
    connect(proc, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), proc,
        &QObject::deleteLater);
    connect(proc, &QObject::destroyed, this, [this, proc]() {
        if (m_process == proc)
            m_process = nullptr;
    });

    // --primary: read PRIMARY (the mouse selection), matching X11 behaviour.
    // --no-newline: drop the trailing newline wl-paste would otherwise add.
    proc->start(kWlPaste, {QStringLiteral("--primary"), QStringLiteral("--no-newline")});
}
