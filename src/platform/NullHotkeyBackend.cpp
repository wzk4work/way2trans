#include "NullHotkeyBackend.h"

#include "Logging.h"

bool NullHotkeyBackend::registerAction(
    const QString &actionId, const QKeySequence &sequence, const QString &description)
{
    Q_UNUSED(sequence);
    Q_UNUSED(description);
    // No global grab. The action is still reachable via the D-Bus/CLI path:
    //   way2trans trigger <action>
    qCInfo(w2tPlatform) << "NullHotkeyBackend: no global grab for" << actionId
                        << "- bind `way2trans trigger` in your desktop settings";
    return false;
}
