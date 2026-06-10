#pragma once

#include <QByteArrayView>
#include <QImage>
#include <linux/videodev2.h>

class FrameConverter
{
public:
    enum FilterMode {
        Natural = 0,
        BackgroundBlur = 1,
        BlackAndWhite = 2,
        Comic = 3
    };

    static bool isSupportedPixelFormat(quint32 pixelFormat);
    static bool isFrameSizeValid(quint32 pixelFormat, int width, int height, qsizetype bytesUsed);
    static QImage toImage(quint32 pixelFormat, int width, int height, QByteArrayView encodedFrame);
    static QImage applyFilter(const QImage &image, int filterMode);

private:
    static QImage convertYuyvToImage(int width, int height, QByteArrayView frame);
    static QImage applyBackgroundBlur(const QImage &image);
    static QImage applyBlackAndWhite(const QImage &image);
    static QImage applyComic(const QImage &image);
};
