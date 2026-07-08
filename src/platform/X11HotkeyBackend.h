#pragma once

#include "HotkeyBackend.h"

#include <QAbstractNativeEventFilter>
#include <QHash>

// xcb_key_symbols_t is `typedef struct _XCBKeySymbols` in <xcb/xcb_keysyms.h>;
// forward-declare the underlying struct (not the typedef) to avoid a conflicting
// declaration when the .cpp includes the real header.
struct _XCBKeySymbols;
using xcb_key_symbols_t = struct _XCBKeySymbols;

// X11 global hotkey backend via xcb XGrabKey (DESIGN.md §4.2).
//
// Grabs keys on the root window and listens for KeyPress events through a Qt
// native event filter. To survive the state of the lock keys, each shortcut is
// grabbed in all four combinations of NumLock (Mod2) and CapsLock (Lock).
//
// Compiled only when the optional xcb-keysyms dependency is present
// (WAY2TRANS_HAVE_XCB_KEYSYMS). When absent, PlatformFactory falls back to the
// Null backend and this translation unit is not built at all.
class X11HotkeyBackend : public HotkeyBackend, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit X11HotkeyBackend(QObject *parent = nullptr);
    ~X11HotkeyBackend() override;

    bool registerAction(const QString &actionId, const QKeySequence &sequence, const QString &description) override;
    bool canRebindAtRuntime() const override { return true; }
    QString name() const override { return QStringLiteral("x11-xgrabkey"); }

    // QAbstractNativeEventFilter
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    struct Grab {
        quint32 keycode = 0;
        quint32 modifiers = 0; // base X modifier mask (no lock variants)
        QString actionId;
    };

    // Map a Qt key sequence to an X keycode + base modifier mask. Returns false
    // if the key cannot be mapped.
    bool resolveSequence(const QKeySequence &sequence, quint32 &keycode, quint32 &modifiers) const;

    // Grab keycode+modifiers in all NumLock/CapsLock variants.
    bool grabAllVariants(quint32 keycode, quint32 modifiers);

    xcb_key_symbols_t *m_keySymbols = nullptr;
    quint32 m_rootWindow = 0;
    QList<Grab> m_grabs;
    // Extra modifier masks (lock keys) to combine with each base grab.
    QList<quint32> m_lockVariants;
};
