#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

class QSettings;

// Application configuration singleton (DESIGN.md §10).
//
// Backed by a QSettings INI file at ~/.config/way2trans/way2trans.conf.
// Config changes are broadcast through settings-specific signals so listeners
// can react without a restart. M0 only wires up a handful of example keys; the
// full group layout ([general]/[hotkeys]/[engines/<id>]/...) grows in later
// milestones.
class Settings : public QObject {
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

    // Names of the child groups directly under `group` (e.g. the service
    // instance ids under "services"). Empty list when the group is absent.
    QStringList childGroups(const QString &group) const;

    // Remove a key, or a whole group with all keys under it.
    void remove(const QString &keyOrGroup);

    // Force a flush to disk (QSettings normally syncs lazily).
    void sync();

signals:
    void sourceLangRuleChanged(const QString &value);
    void targetLangChanged(const QString &value);
    void autostartChanged(bool enabled);

private:
    explicit Settings(QObject *parent = nullptr);
    Q_DISABLE_COPY(Settings)

    // All reads go through here: retries "general/x" as "General/x" to survive
    // the Qt 6.4 INI [%General] escaping asymmetry (see the .cpp comment).
    QVariant readValue(const QString &key, const QVariant &defaultValue) const;

    QSettings *m_settings;
};
