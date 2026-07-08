#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

class QSettings;

// Application configuration singleton (DESIGN.md §10).
//
// Backed by a QSettings INI file at ~/.config/way2trans/way2trans.conf.
// Config changes are broadcast through settings-specific signals so listeners
// can react without a restart. M0 only wires up a handful of example keys; the
// full group layout ([general]/[hotkeys]/[engines/<id>]/...) grows in later
// milestones.
class Settings : public QObject
{
    Q_OBJECT

public:
    static Settings &instance();

    // Absolute path to the backing INI file.
    static QString configFilePath();

    // [general]
    QString sourceLangRule() const;
    void setSourceLangRule(const QString &value);

    QString targetLang() const;
    void setTargetLang(const QString &value);

    bool autostart() const;
    void setAutostart(bool enabled);

    // Generic access for keys not yet promoted to typed accessors.
    QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    void setValue(const QString &key, const QVariant &value);

    // Force a flush to disk (QSettings normally syncs lazily).
    void sync();

signals:
    void sourceLangRuleChanged(const QString &value);
    void targetLangChanged(const QString &value);
    void autostartChanged(bool enabled);

private:
    explicit Settings(QObject *parent = nullptr);
    Q_DISABLE_COPY(Settings)

    QSettings *m_settings;
};
