#pragma once

#include <QByteArrayView>
#include <QImage>
#include <linux/videodev2.h>

class FrameConverter
{
public:
    static bool isSupportedPixelFormat(quint32 pixelFormat);
    static bool isFrameSizeValid(quint32 pixelFormat, int width, int height, qsizetype bytesUsed);
    static QImage toImage(quint32 pixelFormat, int width, int height, QByteArrayView encodedFrame);

private:
    static QImage convertYuyvToImage(int width, int height, QByteArrayView frame);
};
