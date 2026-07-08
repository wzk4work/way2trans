#pragma once

#include "HotkeyBackend.h"

#include <QDBusObjectPath>
#include <QHash>
#include <QString>
#include <QVariantMap>

class QDBusInterface;

// Wayland global hotkey backend via the GlobalShortcuts portal (DESIGN.md §4.2).
//
// Talks org.freedesktop.portal.GlobalShortcuts over the session bus:
//   CreateSession -> BindShortcuts -> listen for Activated(session, shortcutId).
// The first BindShortcuts triggers a compositor authorisation dialog (KDE
// Plasma, Hyprland). The session handle + token are persisted to Settings so a
// later run reuses the same session and avoids re-prompting.
//
// registerAction() maps a way2trans actionId ("translate-selection", ...) to a
// portal shortcut id and asks the portal to bind it. When the portal interface
// is not present (e.g. no xdg-desktop-portal, or a backend without the
// GlobalShortcuts implementation) registerAction() returns false so
// PlatformFactory stacks the Null backend for that action (manual binding).
//
// Note on key sequences: the GlobalShortcuts portal does not let the app force a
// specific accelerator. The requested QKeySequence is passed only as a
// preferred-trigger hint; the compositor's settings UI owns the final binding.
class PortalHotkeyBackend : public HotkeyBackend {
    Q_OBJECT

public:
    explicit PortalHotkeyBackend(QObject *parent = nullptr);
    ~PortalHotkeyBackend() override;

    bool registerAction(const QString &actionId, const QKeySequence &sequence, const QString &description) override;
    bool canRebindAtRuntime() const override { return false; }
    QString name() const override { return QStringLiteral("portal-globalshortcuts"); }

    // Whether the org.freedesktop.portal.GlobalShortcuts interface is reachable
    // on the session bus. PlatformFactory probes this before choosing this
    // backend. Cheap: an introspection round-trip, no session created.
    static bool isPortalAvailable();

private:
    // Ensure a portal session exists (create + persist handle/token). Returns
    // false if the portal is unavailable or session creation cannot start.
    bool ensureSession();

    // Portal shortcut id <-> way2trans actionId are kept identical; this holds
    // the pending bind requests until the session is up.
    struct PendingBind {
        QString actionId;
        QString description;
        QString preferredTrigger;
    };

    void flushPendingBinds();

private slots:
    // org.freedesktop.portal.Request::Response for CreateSession:
    // (u response, a{sv} results) with results["session_handle"] = path string.
    void handleSessionResponse(uint response, const QVariantMap &results);

    // org.freedesktop.portal.GlobalShortcuts::Activated(o session, s shortcutId, ...).
    void onActivated(const QDBusObjectPath &sessionHandle, const QString &shortcutId);

private:
    QDBusInterface *m_portal = nullptr;
    QString m_sessionHandle; // object path of the live portal session
    QString m_sessionToken; // persisted token used to recreate the session
    bool m_sessionRequested = false;
    QList<PendingBind> m_pending;
    QHash<QString, QString> m_bound; // shortcutId -> actionId
};
