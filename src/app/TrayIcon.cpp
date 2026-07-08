#include "TrayIcon.h"

#include "Application.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

TrayIcon::TrayIcon(Application *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_tray(new QSystemTrayIcon(this))
    , m_menu(new QMenu)
{
    // No bundled artwork yet; fall back to a standard theme icon so the tray
    // entry is visible during M0.
    QIcon icon = QIcon::fromTheme(QStringLiteral("accessories-dictionary"));
    if (icon.isNull())
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    m_tray->setIcon(icon);
    m_tray->setToolTip(QStringLiteral("way2trans"));

    auto *selectionAction = m_menu->addAction(tr("Translate Selection"));
    connect(selectionAction, &QAction::triggered, m_app, &Application::triggerSelection);

    auto *captureAction = m_menu->addAction(tr("Translate Screenshot"));
    connect(captureAction, &QAction::triggered, m_app, &Application::triggerCapture);

    auto *clipboardAction = m_menu->addAction(tr("Translate Clipboard"));
    connect(clipboardAction, &QAction::triggered, m_app, &Application::triggerClipboard);

    m_menu->addSeparator();

    auto *showMainAction = m_menu->addAction(tr("Show Main Window"));
    connect(showMainAction, &QAction::triggered, m_app, &Application::showMainWindow);

    auto *settingsAction = m_menu->addAction(tr("Settings..."));
    connect(settingsAction, &QAction::triggered, m_app, &Application::openSettings);

    m_menu->addSeparator();

    auto *quitAction = m_menu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, m_app, &Application::quit);

    m_tray->setContextMenu(m_menu);

    // Left-click / double-click activation opens the main window.
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            m_app->showMainWindow();
    });
}

TrayIcon::~TrayIcon()
{
    // m_menu is a top-level QMenu with no QObject parent; delete it explicitly.
    delete m_menu;
}

bool TrayIcon::isAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayIcon::show()
{
    m_tray->show();
}

void TrayIcon::notifyNoSelection()
{
    if (m_tray->isVisible() && QSystemTrayIcon::supportsMessages()) {
        m_tray->showMessage(
            QStringLiteral("way2trans"), tr("No selected text detected"), QSystemTrayIcon::Information, 1500);
    }
}
