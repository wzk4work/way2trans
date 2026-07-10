#include "Settings.h"

#include "Logging.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace {
// Example config keys (M0). Kept as constants to avoid stringly-typed drift.
constexpr auto kKeySourceLangRule = "general/sourceLangRule";
constexpr auto kKeyTargetLang = "general/targetLang";
constexpr auto kKeyAutostart = "general/autostart";

constexpr auto kDefaultSourceLangRule = "auto";
constexpr auto kDefaultTargetLang = "en";
} // namespace

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

QString Settings::configFilePath()
{
    // ~/.config/way2trans/way2trans.conf (DESIGN.md §10).
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(base).filePath(QStringLiteral("way2trans/way2trans.conf"));
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(configFilePath(), QSettings::IniFormat, this))
{
    qCInfo(w2tCore) << "Settings backed by" << configFilePath();
}

QString Settings::sourceLangRule() const
{
    return m_settings->value(kKeySourceLangRule, kDefaultSourceLangRule).toString();
}

void Settings::setSourceLangRule(const QString &value)
{
    if (sourceLangRule() == value)
        return;
    m_settings->setValue(kKeySourceLangRule, value);
    emit sourceLangRuleChanged(value);
}

QString Settings::targetLang() const
{
    return m_settings->value(kKeyTargetLang, kDefaultTargetLang).toString();
}

void Settings::setTargetLang(const QString &value)
{
    if (targetLang() == value)
        return;
    m_settings->setValue(kKeyTargetLang, value);
    emit targetLangChanged(value);
}

bool Settings::autostart() const
{
    return m_settings->value(kKeyAutostart, false).toBool();
}

void Settings::setAutostart(bool enabled)
{
    if (autostart() == enabled)
        return;
    m_settings->setValue(kKeyAutostart, enabled);
    emit autostartChanged(enabled);
}

QVariant Settings::value(const QString &key, const QVariant &defaultValue) const
{
    return m_settings->value(key, defaultValue);
}

void Settings::setValue(const QString &key, const QVariant &value)
{
    m_settings->setValue(key, value);
}

QStringList Settings::childGroups(const QString &group) const
{
    m_settings->beginGroup(group);
    const QStringList groups = m_settings->childGroups();
    m_settings->endGroup();
    return groups;
}

void Settings::remove(const QString &keyOrGroup)
{
    m_settings->remove(keyOrGroup);
}

void Settings::sync()
{
    m_settings->sync();
}
