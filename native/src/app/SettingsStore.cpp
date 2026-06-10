#include "SettingsStore.h"

#include <QSettings>

namespace {
constexpr auto preferredDevicePathKey = "camera/preferredDevicePath";
constexpr auto preferredFormatIndexKey = "camera/preferredFormatIndex";
}

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
{
}

QString SettingsStore::preferredDevicePath() const
{
    QSettings settings;
    return settings.value(QString::fromLatin1(preferredDevicePathKey)).toString();
}

void SettingsStore::setPreferredDevicePath(const QString &devicePath)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(preferredDevicePathKey), devicePath);
}

int SettingsStore::preferredFormatIndex() const
{
    QSettings settings;
    return settings.value(QString::fromLatin1(preferredFormatIndexKey), 0).toInt();
}

void SettingsStore::setPreferredFormatIndex(int index)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(preferredFormatIndexKey), index);
}
