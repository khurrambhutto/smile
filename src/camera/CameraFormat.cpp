#include "CameraFormat.h"

#include <QtGlobal>

bool CameraFormat::isValid() const
{
    return pixelFormat != 0 && width > 0 && height > 0;
}

QString CameraFormat::description() const
{
    const QString formatName = pixelFormatName.isEmpty()
        ? v4l2PixelFormatDisplayName(pixelFormat)
        : pixelFormatName;

    if (framesPerSecond > 0) {
        return QStringLiteral("%1 • %2×%3 • %4 FPS")
            .arg(formatName)
            .arg(width)
            .arg(height)
            .arg(framesPerSecond);
    }

    return QStringLiteral("%1 • %2×%3")
        .arg(formatName)
        .arg(width)
        .arg(height);
}

qint64 CameraFormat::preferenceScore() const
{
    qint64 score = qint64(width) * qint64(height);

    if (pixelFormat == V4L2_PIX_FMT_MJPEG) {
        score += 2'000'000'000;
    } else if (pixelFormat == V4L2_PIX_FMT_YUYV) {
        score += 1'000'000'000;
    }

    if (framesPerSecond >= 30) {
        score += 20'000'000;
    } else if (framesPerSecond > 0) {
        score += framesPerSecond * 100'000;
    }

    return score;
}

QString v4l2FourccToString(quint32 pixelFormat)
{
    QString result;
    result.reserve(4);
    result.append(QChar(char(pixelFormat & 0xff)));
    result.append(QChar(char((pixelFormat >> 8) & 0xff)));
    result.append(QChar(char((pixelFormat >> 16) & 0xff)));
    result.append(QChar(char((pixelFormat >> 24) & 0xff)));
    return result;
}

QString v4l2PixelFormatDisplayName(quint32 pixelFormat)
{
    switch (pixelFormat) {
    case V4L2_PIX_FMT_MJPEG:
        return QStringLiteral("MJPEG");
    case V4L2_PIX_FMT_YUYV:
        return QStringLiteral("YUYV");
    default:
        return v4l2FourccToString(pixelFormat);
    }
}
