#pragma once

#include <QString>
#include <linux/videodev2.h>

struct CameraFormat
{
    quint32 pixelFormat = 0;
    QString pixelFormatName;
    int width = 0;
    int height = 0;
    int framesPerSecond = 0;

    bool isValid() const;
    QString description() const;
    qint64 preferenceScore() const;
};

QString v4l2FourccToString(quint32 pixelFormat);
QString v4l2PixelFormatDisplayName(quint32 pixelFormat);
