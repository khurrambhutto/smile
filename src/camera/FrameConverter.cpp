#include "FrameConverter.h"

#include <QPainter>
#include <QRadialGradient>
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

QImage FrameConverter::applyFilter(const QImage &image, int filterMode)
{
    if (image.isNull()) {
        return {};
    }

    switch (filterMode) {
    case BackgroundBlur:
        return applyBackgroundBlur(image);
    case BlackAndWhite:
        return applyBlackAndWhite(image);
    case Comic:
        return applyComic(image);
    case Natural:
    default:
        return image;
    }
}

QImage FrameConverter::applyBackgroundBlur(const QImage &image)
{
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    if (source.isNull()) {
        return {};
    }

    const QSize blurSize(
        std::max(24, source.width() / 18),
        std::max(24, source.height() / 18));
    QImage blurred = source
        .scaled(blurSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QImage result = blurred.convertToFormat(QImage::Format_ARGB32);
    QImage mask(source.size(), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);

    QPainter maskPainter(&mask);
    maskPainter.setRenderHint(QPainter::Antialiasing);
    QRadialGradient gradient(QPointF(source.width() * 0.5, source.height() * 0.45), source.width() * 0.42);
    gradient.setColorAt(0.0, QColor(255, 255, 255, 255));
    gradient.setColorAt(0.72, QColor(255, 255, 255, 230));
    gradient.setColorAt(1.0, QColor(255, 255, 255, 0));
    maskPainter.setBrush(gradient);
    maskPainter.setPen(Qt::NoPen);
    maskPainter.drawEllipse(QPointF(source.width() * 0.5, source.height() * 0.45), source.width() * 0.31, source.height() * 0.43);
    maskPainter.end();

    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(0, 0, mask);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOver);
    painter.drawImage(0, 0, blurred);
    painter.end();

    return result;
}

QImage FrameConverter::applyBlackAndWhite(const QImage &image)
{
    return image.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32);
}

QImage FrameConverter::applyComic(const QImage &image)
{
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    if (source.isNull()) {
        return {};
    }

    QImage result(source.size(), QImage::Format_ARGB32);

    for (int y = 0; y < source.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(result.scanLine(y));

        for (int x = 0; x < source.width(); ++x) {
            const QColor color(src[x]);
            const int r = (color.red() / 48) * 48 + 24;
            const int g = (color.green() / 48) * 48 + 24;
            const int b = (color.blue() / 48) * 48 + 24;

            int edge = 0;
            if (x > 0 && y > 0 && x + 1 < source.width() && y + 1 < source.height()) {
                const QRgb left = src[x - 1];
                const QRgb right = src[x + 1];
                const QRgb up = reinterpret_cast<const QRgb *>(source.constScanLine(y - 1))[x];
                const QRgb down = reinterpret_cast<const QRgb *>(source.constScanLine(y + 1))[x];
                edge = std::abs(qGray(left) - qGray(right)) + std::abs(qGray(up) - qGray(down));
            }

            if (edge > 72) {
                dst[x] = qRgb(24, 24, 24);
            } else {
                dst[x] = qRgb(clampToByte(r + 12), clampToByte(g + 8), clampToByte(b));
            }
        }
    }

    return result;
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
