#pragma once

#include "CameraDevice.h"

#include <QObject>

class DeviceManager : public QObject
{
    Q_OBJECT

public:
    explicit DeviceManager(QObject *parent = nullptr);

    QList<CameraDevice> scanDevices(QString *errorMessage = nullptr) const;

private:
    CameraDevice inspectDevice(const QString &devicePath) const;
    QList<CameraFormat> enumerateFormats(int fileDescriptor) const;
    QList<CameraFormat> enumerateFrameSizes(int fileDescriptor, quint32 pixelFormat, const QString &pixelFormatName) const;
    QList<int> enumerateFrameRates(int fileDescriptor, quint32 pixelFormat, int width, int height) const;
};
