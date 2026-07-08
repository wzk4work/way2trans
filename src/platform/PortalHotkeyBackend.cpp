#include "PortalHotkeyBackend.h"

#include "Logging.h"
#include "Settings.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QUuid>
#include <QVariantMap>

// GlobalShortcuts portal glue. Spec:
//   https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html
//
// The portal is request/response based: methods return an object path for an
// org.freedesktop.portal.Request whose Response signal carries the actual
// result. We keep it minimal for M2: create one session, bind our shortcuts,
// and route Activated back to actionIds. Since this environment is X11 the code
// path is exercised only by compilation + the availability probe; runtime is
// verified later on KDE/Sway.
namespace {
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kShortcutsIface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");

// A shortcut definition is (id, a{sv}); the portal takes an array of those.
// We register the type so QDBus can marshal it.
using ShortcutEntry = QPair<QString, QVariantMap>;
using ShortcutList = QList<ShortcutEntry>;

// Unique token for a Request/Session handle (must be a valid path element).
QString newToken(const QString &prefix)
{
    QString t = QUuid::createUuid().toString(QUuid::Id128);
    return prefix + t;
}
} // namespace

Q_DECLARE_METATYPE(ShortcutEntry)
Q_DECLARE_METATYPE(ShortcutList)

PortalHotkeyBackend::PortalHotkeyBackend(QObject *parent)
    : HotkeyBackend(parent)
{
    qDBusRegisterMetaType<ShortcutEntry>();
    qDBusRegisterMetaType<ShortcutList>();

    m_portal = new QDBusInterface(kPortalService, kPortalPath, kShortcutsIface, QDBusConnection::sessionBus(), this);

    // Reuse a persisted session token so the compositor does not re-prompt.
    m_sessionToken = Settings::instance().value(QStringLiteral("platform/portal-shortcuts-token")).toString();

    // Route the portal's Activated(o session_handle, s shortcut_id, t timestamp,
    // a{sv} options) signal to our actionIds. We only need the first two args;
    // QDBus matches the leading arguments and ignores the trailing ones.
    QDBusConnection::sessionBus().connect(kPortalService, kPortalPath, kShortcutsIface, QStringLiteral("Activated"),
        this, SLOT(onActivated(QDBusObjectPath, QString)));
}

PortalHotkeyBackend::~PortalHotkeyBackend() = default;

bool PortalHotkeyBackend::isPortalAvailable()
{
    // A lightweight existence check: is the interface introspectable? We avoid
    // creating a session here so probing has no side effects.
    QDBusInterface iface(kPortalService, kPortalPath, kShortcutsIface, QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        qCDebug(w2tPlatform) << "GlobalShortcuts portal not available:" << iface.lastError().message();
        return false;
    }
    // isValid() succeeds if the service can be activated; confirm the property
    // "version" resolves so we know the GlobalShortcuts interface really exists
    // rather than just the portal service.
    const QVariant version = iface.property("version");
    if (!version.isValid()) {
        qCDebug(w2tPlatform) << "GlobalShortcuts portal present but no version property; treating as unavailable";
        return false;
    }
    qCInfo(w2tPlatform) << "GlobalShortcuts portal available, version" << version.toUInt();
    return true;
}

bool PortalHotkeyBackend::ensureSession()
{
    if (!m_sessionHandle.isEmpty())
        return true; // already have a live session
    if (m_sessionRequested)
        return true; // creation already in flight; binds will flush on completion
    if (!m_portal || !m_portal->isValid()) {
        qCWarning(w2tPlatform) << "PortalHotkeyBackend: portal interface invalid; cannot create session";
        return false;
    }

    // Persist a stable session token on first use so re-runs reuse it.
    if (m_sessionToken.isEmpty()) {
        m_sessionToken = newToken(QStringLiteral("way2trans_shortcuts_"));
        Settings::instance().setValue(QStringLiteral("platform/portal-shortcuts-token"), m_sessionToken);
    }

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), newToken(QStringLiteral("way2trans_req_")));
    options.insert(QStringLiteral("session_handle_token"), m_sessionToken);

    QDBusReply<QDBusObjectPath> reply = m_portal->call(QStringLiteral("CreateSession"), options);
    if (!reply.isValid()) {
        qCWarning(w2tPlatform) << "PortalHotkeyBackend: CreateSession failed:" << reply.error().message();
        return false;
    }

    m_sessionRequested = true;

    // Listen for the Request's Response(u response, a{sv} results). The results
    // map carries session_handle when response == 0 (success).
    const QString requestPath = reply.value().path();
    QDBusConnection::sessionBus().connect(kPortalService, requestPath, QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"), this, SLOT(handleSessionResponse(uint, QVariantMap)));
    return true;
}

void PortalHotkeyBackend::handleSessionResponse(uint response, const QVariantMap &results)
{
    if (response != 0) {
        // Non-zero: user cancelled the authorisation dialog or it errored.
        qCWarning(w2tPlatform) << "PortalHotkeyBackend: session request denied (response" << response << ")";
        m_sessionRequested = false;
        m_pending.clear();
        return;
    }
    m_sessionHandle = results.value(QStringLiteral("session_handle")).toString();
    if (m_sessionHandle.isEmpty()) {
        qCWarning(w2tPlatform) << "PortalHotkeyBackend: session Response missing session_handle";
        m_sessionRequested = false;
        return;
    }
    qCInfo(w2tPlatform) << "PortalHotkeyBackend: session created" << m_sessionHandle;
    flushPendingBinds();
}

void PortalHotkeyBackend::flushPendingBinds()
{
    if (m_sessionHandle.isEmpty() || m_pending.isEmpty())
        return;
    if (!m_portal || !m_portal->isValid())
        return;

    ShortcutList shortcuts;
    for (const PendingBind &b : m_pending) {
        QVariantMap meta;
        meta.insert(QStringLiteral("description"), b.description);
        if (!b.preferredTrigger.isEmpty())
            meta.insert(QStringLiteral("preferred_trigger"), b.preferredTrigger);
        shortcuts.append(qMakePair(b.actionId, meta));
        m_bound.insert(b.actionId, b.actionId);
    }
    m_pending.clear();

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), newToken(QStringLiteral("way2trans_bind_")));

    // BindShortcuts(session, shortcuts, parent_window, options). parent_window is
    // an X11/Wayland handle string; empty is allowed (portal picks a default).
    QDBusReply<QDBusObjectPath> reply = m_portal->call(QStringLiteral("BindShortcuts"),
        QVariant::fromValue(QDBusObjectPath(m_sessionHandle)), QVariant::fromValue(shortcuts), QString(), options);
    if (!reply.isValid())
        qCWarning(w2tPlatform) << "PortalHotkeyBackend: BindShortcuts failed:" << reply.error().message();
    else
        qCInfo(w2tPlatform) << "PortalHotkeyBackend: BindShortcuts requested for" << m_bound.keys();
}

bool PortalHotkeyBackend::registerAction(
    const QString &actionId, const QKeySequence &sequence, const QString &description)
{
    if (!m_portal || !m_portal->isValid()) {
        qCWarning(w2tPlatform) << "PortalHotkeyBackend: portal unavailable; cannot register" << actionId;
        return false;
    }

    m_pending.append(PendingBind {actionId, description, sequence.toString(QKeySequence::PortableText)});

    if (!ensureSession())
        return false;

    // If the session already exists, bind immediately; otherwise the bind flushes
    // when the session Response arrives.
    flushPendingBinds();

    // We return true optimistically: the portal accepted the request. The final
    // accelerator is owned by the compositor's settings; failure to actually bind
    // is surfaced through logs, not a false return (which would wrongly fall the
    // caller back to Null after we already created a session).
    qCInfo(w2tPlatform) << "PortalHotkeyBackend: registration requested for" << actionId << "(preferred"
                        << sequence.toString() << ")";
    return true;
}

void PortalHotkeyBackend::onActivated(const QDBusObjectPath &sessionHandle, const QString &shortcutId)
{
    Q_UNUSED(sessionHandle);
    const auto it = m_bound.constFind(shortcutId);
    const QString actionId = it != m_bound.constEnd() ? it.value() : shortcutId;
    qCDebug(w2tPlatform) << "PortalHotkeyBackend: activated" << shortcutId << "->" << actionId;
    emit activated(actionId);
}
