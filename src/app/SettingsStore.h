#pragma once

#include <QObject>
#include <QString>

class QSettings;

class SettingsStore : public QObject
{
    Q_OBJECT

public:
    explicit SettingsStore(QObject *parent = nullptr);

    QString preferredDevicePath() const;
    void setPreferredDevicePath(const QString &devicePath);

    int preferredFormatIndex() const;
    void setPreferredFormatIndex(int index);
};
