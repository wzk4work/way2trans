#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

// Global hotkey registration (DESIGN.md §4.2).
//
// actionId is one of "translate-selection" | "translate-capture" | "show-main"
// | "translate-clipboard". Backends may be stacked: an action that a richer
// backend cannot register can fall back to the Null backend's manual-binding
// guidance, so this is not an all-or-nothing choice.
class HotkeyBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~HotkeyBackend() override = default;

    // Register a global shortcut for actionId. Returns true if the key grab
    // succeeded. A false return means the caller should surface manual-binding
    // guidance for this action.
    virtual bool registerAction(const QString &actionId, const QKeySequence &sequence, const QString &description) = 0;

    // Whether shortcuts can be re-bound while the app is running.
    virtual bool canRebindAtRuntime() const = 0;

    // Human-readable backend name for logs / the settings page.
    virtual QString name() const = 0;

signals:
    void activated(const QString &actionId);
};
