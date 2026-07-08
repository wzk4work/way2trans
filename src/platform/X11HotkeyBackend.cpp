#include "X11HotkeyBackend.h"

#include "Logging.h"

#include <QGuiApplication>
#include <QKeyCombination>

// Public native-interface header exposing QNativeInterface::QX11Application
// (connection()/display()); no private Qt module needed.
#include <QtGui/qguiapplication_platform.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

// X11/keysymdef keysyms for the modifiers/letters we need. We avoid pulling in
// <X11/keysym.h> to keep this a pure-xcb translation unit.
namespace {
// Base X modifier masks.
constexpr quint32 kShiftMask = XCB_MOD_MASK_SHIFT;
constexpr quint32 kControlMask = XCB_MOD_MASK_CONTROL;
constexpr quint32 kMod1Mask = XCB_MOD_MASK_1; // Alt
constexpr quint32 kMod4Mask = XCB_MOD_MASK_4; // Super/Meta
constexpr quint32 kCapsLockMask = XCB_MOD_MASK_LOCK;
constexpr quint32 kNumLockMask = XCB_MOD_MASK_2; // Mod2 is NumLock on typical layouts

xcb_connection_t *x11Connection()
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return native ? native->connection() : nullptr;
}

// Translate a Qt::Key to an X11 keysym. Covers letters, digits and the handful
// of common non-letter action keys; enough for user-configurable shortcuts in
// M1 (default is Ctrl+Alt+D).
quint32 qtKeyToKeysym(int qtKey)
{
    // ASCII letters/digits map straight to their keysym value (Latin-1).
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        return static_cast<quint32>(0x0061 + (qtKey - Qt::Key_A)); // lowercase 'a'..'z'
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return static_cast<quint32>(0x0030 + (qtKey - Qt::Key_0));
    switch (qtKey) {
    case Qt::Key_Space:
        return 0x0020;
    case Qt::Key_F1:
        return 0xFFBE;
    case Qt::Key_F2:
        return 0xFFBF;
    case Qt::Key_F3:
        return 0xFFC0;
    case Qt::Key_F4:
        return 0xFFC1;
    default:
        return 0;
    }
}

quint32 qtModifiersToX(Qt::KeyboardModifiers mods)
{
    quint32 x = 0;
    if (mods & Qt::ShiftModifier)
        x |= kShiftMask;
    if (mods & Qt::ControlModifier)
        x |= kControlMask;
    if (mods & Qt::AltModifier)
        x |= kMod1Mask;
    if (mods & Qt::MetaModifier)
        x |= kMod4Mask;
    return x;
}
} // namespace

X11HotkeyBackend::X11HotkeyBackend(QObject *parent)
    : HotkeyBackend(parent)
{
    xcb_connection_t *conn = x11Connection();
    if (!conn) {
        qCWarning(w2tPlatform) << "X11HotkeyBackend: no xcb connection";
        return;
    }
    m_keySymbols = xcb_key_symbols_alloc(conn);
    const xcb_setup_t *setup = xcb_get_setup(conn);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    m_rootWindow = it.data ? it.data->root : 0;

    // Lock-key variants to OR onto each grab so the shortcut fires regardless of
    // NumLock/CapsLock state (a classic XGrabKey gotcha).
    m_lockVariants = {0, kCapsLockMask, kNumLockMask, kCapsLockMask | kNumLockMask};

    qGuiApp->installNativeEventFilter(this);
}

X11HotkeyBackend::~X11HotkeyBackend()
{
    xcb_connection_t *conn = x11Connection();
    if (conn && m_rootWindow) {
        // Ungrab everything we grabbed.
        for (const Grab &g : m_grabs) {
            for (quint32 lock : m_lockVariants)
                xcb_ungrab_key(conn, g.keycode, m_rootWindow, static_cast<quint16>(g.modifiers | lock));
        }
        xcb_flush(conn);
    }
    if (m_keySymbols)
        xcb_key_symbols_free(m_keySymbols);
    if (qGuiApp)
        qGuiApp->removeNativeEventFilter(this);
}

bool X11HotkeyBackend::resolveSequence(const QKeySequence &sequence, quint32 &keycode, quint32 &modifiers) const
{
    if (sequence.isEmpty() || !m_keySymbols)
        return false;

    const QKeyCombination combo = sequence[0];
    const quint32 keysym = qtKeyToKeysym(combo.key());
    if (keysym == 0)
        return false;

    xcb_keycode_t *codes = xcb_key_symbols_get_keycode(m_keySymbols, keysym);
    if (!codes)
        return false;
    keycode = codes[0];
    free(codes);
    if (keycode == 0)
        return false;

    modifiers = qtModifiersToX(combo.keyboardModifiers());
    return true;
}

bool X11HotkeyBackend::grabAllVariants(quint32 keycode, quint32 modifiers)
{
    xcb_connection_t *conn = x11Connection();
    if (!conn || !m_rootWindow)
        return false;

    bool allOk = true;
    QList<xcb_void_cookie_t> cookies;
    for (quint32 lock : m_lockVariants) {
        xcb_void_cookie_t cookie
            = xcb_grab_key_checked(conn, /*owner_events=*/1, m_rootWindow, static_cast<quint16>(modifiers | lock),
                static_cast<xcb_keycode_t>(keycode), XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        cookies << cookie;
    }
    for (const xcb_void_cookie_t &cookie : cookies) {
        if (xcb_generic_error_t *err = xcb_request_check(conn, cookie)) {
            // BadAccess (code 10) means another client already holds this grab.
            qCWarning(w2tPlatform) << "XGrabKey failed, error code" << err->error_code
                                   << "(another app may own this shortcut)";
            free(err);
            allOk = false;
        }
    }
    xcb_flush(conn);
    return allOk;
}

bool X11HotkeyBackend::registerAction(const QString &actionId, const QKeySequence &sequence, const QString &description)
{
    Q_UNUSED(description);
    quint32 keycode = 0;
    quint32 modifiers = 0;
    if (!resolveSequence(sequence, keycode, modifiers)) {
        qCWarning(w2tPlatform) << "X11HotkeyBackend: cannot map sequence" << sequence.toString() << "for" << actionId;
        return false;
    }

    if (!grabAllVariants(keycode, modifiers)) {
        qCWarning(w2tPlatform) << "X11HotkeyBackend: grab failed for" << actionId << sequence.toString();
        return false;
    }

    m_grabs.append(Grab {keycode, modifiers, actionId});
    qCInfo(w2tPlatform) << "X11HotkeyBackend: registered" << actionId << "=>" << sequence.toString() << "keycode"
                        << keycode << "mods" << modifiers;
    return true;
}

bool X11HotkeyBackend::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);
    if (eventType != QByteArrayLiteral("xcb_generic_event_t"))
        return false;

    auto *ev = static_cast<xcb_generic_event_t *>(message);
    if ((ev->response_type & ~0x80) != XCB_KEY_PRESS)
        return false;

    auto *keyEvent = reinterpret_cast<xcb_key_press_event_t *>(ev);
    // Strip the lock-key bits so a grab registered as Ctrl+Alt+D matches whether
    // or not NumLock/CapsLock happens to be on.
    const quint32 cleanMods = keyEvent->state & ~(kCapsLockMask | kNumLockMask);

    for (const Grab &g : m_grabs) {
        if (g.keycode == keyEvent->detail && g.modifiers == cleanMods) {
            qCDebug(w2tPlatform) << "hotkey activated" << g.actionId;
            emit activated(g.actionId);
            return true;
        }
    }
    return false;
}
