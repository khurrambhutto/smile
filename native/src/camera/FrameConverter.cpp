#include "FrameConverter.h"

#include <algorithm>

namespace {
int clampToByte(int value)
{
    return std::clamp(value, 0, 255);
}
}

bool FrameConverter::isSupportedPixelFormat(quint32 pixelFormat)
{
    return pixelFormat == V4L2_PIX_FMT_MJPEG || pixelFormat == V4L2_PIX_FMT_YUYV;
}

bool FrameConverter::isFrameSizeValid(quint32 pixelFormat, int width, int height, qsizetype bytesUsed)
{
    if (width <= 0 || height <= 0 || bytesUsed <= 0) {
        return false;
    }

    if (pixelFormat == V4L2_PIX_FMT_YUYV) {
        const qsizetype expected = qsizetype(width) * qsizetype(height) * 2;
        return bytesUsed >= expected;
    }

    if (pixelFormat == V4L2_PIX_FMT_MJPEG) {
        return bytesUsed > 4;
    }

    return false;
}

QImage FrameConverter::toImage(quint32 pixelFormat, int width, int height, QByteArrayView encodedFrame)
{
    if (!isFrameSizeValid(pixelFormat, width, height, encodedFrame.size())) {
        return {};
    }

    if (pixelFormat == V4L2_PIX_FMT_MJPEG) {
        QImage image;
        image.loadFromData(reinterpret_cast<const uchar *>(encodedFrame.data()), encodedFrame.size(), "JPG");
        return image;
    }

    if (pixelFormat == V4L2_PIX_FMT_YUYV) {
        return convertYuyvToImage(width, height, encodedFrame);
    }

    return {};
}

QImage FrameConverter::convertYuyvToImage(int width, int height, QByteArrayView frame)
{
    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) {
        return {};
    }

    const auto *src = reinterpret_cast<const uchar *>(frame.data());

    for (int y = 0; y < height; ++y) {
        auto *dst = reinterpret_cast<QRgb *>(image.scanLine(y));
        const int rowOffset = y * width * 2;

        for (int x = 0; x < width; x += 2) {
            const int offset = rowOffset + x * 2;
            const int y0 = int(src[offset + 0]);
            const int u = int(src[offset + 1]) - 128;
            const int y1 = int(src[offset + 2]);
            const int v = int(src[offset + 3]) - 128;

            const auto toRgb = [u, v](int luma) {
                const int c = luma - 16;
                const int d = u;
                const int e = v;
                const int r = clampToByte((298 * c + 409 * e + 128) >> 8);
                const int g = clampToByte((298 * c - 100 * d - 208 * e + 128) >> 8);
                const int b = clampToByte((298 * c + 516 * d + 128) >> 8);
                return qRgb(r, g, b);
            };

            dst[x] = toRgb(y0);
            if (x + 1 < width) {
                dst[x + 1] = toRgb(y1);
            }
        }
    }

    return image;
}
