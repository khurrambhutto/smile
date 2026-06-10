#include "DeviceManager.h"

#include "FrameConverter.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSize>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
int xioctl(int fileDescriptor, unsigned long request, void *arg)
{
    int result = -1;
    do {
        result = ioctl(fileDescriptor, request, arg);
    } while (result == -1 && errno == EINTR);
    return result;
}

int videoDeviceNumber(const QString &path)
{
    static const QRegularExpression expression(QStringLiteral("video(\\d+)$"));
    const auto match = expression.match(path);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

int frameIntervalToFps(const v4l2_fract &interval)
{
    if (interval.numerator == 0 || interval.denominator == 0) {
        return 0;
    }

    return qRound(double(interval.denominator) / double(interval.numerator));
}

void appendUniqueFrameRate(QList<int> &rates, int fps)
{
    if (fps > 0 && !rates.contains(fps)) {
        rates.append(fps);
    }
}
}

DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent)
{
}

QList<CameraDevice> DeviceManager::scanDevices(QString *errorMessage) const
{
    QDir devDir(QStringLiteral("/dev"));
    QStringList entries = devDir.entryList(QStringList{QStringLiteral("video*")}, QDir::System | QDir::Readable, QDir::Name);

    std::sort(entries.begin(), entries.end(), [](const QString &left, const QString &right) {
        return videoDeviceNumber(left) < videoDeviceNumber(right);
    });

    QList<CameraDevice> devices;

    for (const QString &entry : entries) {
        const QString path = devDir.absoluteFilePath(entry);
        const CameraDevice device = inspectDevice(path);
        if (device.isValid()) {
            devices.append(device);
        }
    }

    if (devices.isEmpty() && errorMessage) {
        *errorMessage = QStringLiteral("No compatible V4L2 camera found. Connect a webcam or check /dev/video* permissions.");
    }

    return devices;
}

CameraDevice DeviceManager::inspectDevice(const QString &devicePath) const
{
    CameraDevice device;
    device.path = devicePath;

    const int fd = open(devicePath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK, 0);
    if (fd == -1) {
        return device;
    }

    v4l2_capability capability {};
    if (xioctl(fd, VIDIOC_QUERYCAP, &capability) == -1) {
        close(fd);
        return device;
    }

    const quint32 effectiveCaps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS)
        ? capability.device_caps
        : capability.capabilities;

    if (!(effectiveCaps & V4L2_CAP_VIDEO_CAPTURE) || !(effectiveCaps & V4L2_CAP_STREAMING)) {
        close(fd);
        return device;
    }

    device.displayName = QString::fromLocal8Bit(reinterpret_cast<const char *>(capability.card)).trimmed();
    device.driverName = QString::fromLocal8Bit(reinterpret_cast<const char *>(capability.driver)).trimmed();
    device.busInfo = QString::fromLocal8Bit(reinterpret_cast<const char *>(capability.bus_info)).trimmed();

    if (device.displayName.isEmpty()) {
        device.displayName = QFileInfo(devicePath).fileName();
    }

    device.displayName = QStringLiteral("%1 (%2)").arg(device.displayName, devicePath);
    device.formats = enumerateFormats(fd);

    close(fd);
    return device;
}

QList<CameraFormat> DeviceManager::enumerateFormats(int fileDescriptor) const
{
    QList<CameraFormat> formats;

    for (quint32 index = 0;; ++index) {
        v4l2_fmtdesc formatDescription {};
        formatDescription.index = index;
        formatDescription.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (xioctl(fileDescriptor, VIDIOC_ENUM_FMT, &formatDescription) == -1) {
            if (errno == EINVAL) {
                break;
            }
            break;
        }

        if (!FrameConverter::isSupportedPixelFormat(formatDescription.pixelformat)) {
            continue;
        }

        const QString pixelFormatName = v4l2PixelFormatDisplayName(formatDescription.pixelformat);
        formats.append(enumerateFrameSizes(fileDescriptor, formatDescription.pixelformat, pixelFormatName));
    }

    std::sort(formats.begin(), formats.end(), [](const CameraFormat &left, const CameraFormat &right) {
        if (left.preferenceScore() != right.preferenceScore()) {
            return left.preferenceScore() > right.preferenceScore();
        }
        return left.description() < right.description();
    });

    return formats;
}

QList<CameraFormat> DeviceManager::enumerateFrameSizes(int fileDescriptor, quint32 pixelFormat, const QString &pixelFormatName) const
{
    QList<CameraFormat> formats;

    for (quint32 index = 0;; ++index) {
        v4l2_frmsizeenum frameSize {};
        frameSize.index = index;
        frameSize.pixel_format = pixelFormat;

        if (xioctl(fileDescriptor, VIDIOC_ENUM_FRAMESIZES, &frameSize) == -1) {
            if (errno == EINVAL) {
                break;
            }
            break;
        }

        QList<QSize> sizes;
        if (frameSize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            sizes.append(QSize(int(frameSize.discrete.width), int(frameSize.discrete.height)));
        } else if (frameSize.type == V4L2_FRMSIZE_TYPE_STEPWISE || frameSize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
            sizes.append(QSize(int(frameSize.stepwise.max_width), int(frameSize.stepwise.max_height)));
            sizes.append(QSize(1920, 1080));
            sizes.append(QSize(1280, 720));
            sizes.append(QSize(int(frameSize.stepwise.min_width), int(frameSize.stepwise.min_height)));
        }

        for (const QSize &size : sizes) {
            if (!size.isValid()) {
                continue;
            }

            const QList<int> rates = enumerateFrameRates(fileDescriptor, pixelFormat, size.width(), size.height());
            if (rates.isEmpty()) {
                formats.append(CameraFormat{pixelFormat, pixelFormatName, size.width(), size.height(), 30});
                continue;
            }

            for (int fps : rates) {
                formats.append(CameraFormat{pixelFormat, pixelFormatName, size.width(), size.height(), fps});
            }
        }
    }

    return formats;
}

QList<int> DeviceManager::enumerateFrameRates(int fileDescriptor, quint32 pixelFormat, int width, int height) const
{
    QList<int> rates;

    for (quint32 index = 0;; ++index) {
        v4l2_frmivalenum interval {};
        interval.index = index;
        interval.pixel_format = pixelFormat;
        interval.width = quint32(width);
        interval.height = quint32(height);

        if (xioctl(fileDescriptor, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == -1) {
            if (errno == EINVAL) {
                break;
            }
            break;
        }

        if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            appendUniqueFrameRate(rates, frameIntervalToFps(interval.discrete));
        } else if (interval.type == V4L2_FRMIVAL_TYPE_STEPWISE || interval.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
            appendUniqueFrameRate(rates, frameIntervalToFps(interval.stepwise.max));
            appendUniqueFrameRate(rates, 30);
            appendUniqueFrameRate(rates, frameIntervalToFps(interval.stepwise.min));
            break;
        }
    }

    std::sort(rates.begin(), rates.end(), std::greater<int>());
    return rates;
}
