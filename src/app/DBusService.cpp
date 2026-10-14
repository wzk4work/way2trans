#include "DBusService.h"

#include "Application.h"

DBusService::DBusService(Application *app)
    : QDBusAbstractAdaptor(app)
    , m_app(app)
{ }

void DBusService::TriggerSelection()
{
    m_app->triggerSelection();
}

void DBusService::TriggerCapture()
{
    m_app->triggerCapture();
}

void DBusService::TriggerCaptureOcr()
{
    m_app->triggerCaptureOcr();
}

void DBusService::TriggerClipboard()
{
    m_app->triggerClipboard();
}

void DBusService::ShowMain()
{
    m_app->showMainWindow();
}

QString DBusService::Translate(const QString &text)
{
    return m_app->translate(text);
}
