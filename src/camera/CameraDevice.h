#pragma once

#include "CameraFormat.h"

#include <QList>
#include <QString>

struct CameraDevice
{
    QString path;
    QString displayName;
    QString driverName;
    QString busInfo;
    QList<CameraFormat> formats;

    bool isValid() const;
};
