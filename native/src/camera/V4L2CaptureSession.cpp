#include "V4L2CaptureSession.h"

#include "FrameConverter.h"
#include "capture/ImageCaptureHandler.h"

#include <QByteArrayView>
#include <QSocketNotifier>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace {
constexpr int requestedBufferCount = 4;
constexpr int maxFramesPerNotifierActivation = 8;
constexpr int stillCaptureTimeoutMs = 3000;

int xioctl(int fileDescriptor, unsigned long request, void *arg)
{
    int result = -1;
    do {
        result = ioctl(fileDescriptor, request, arg);
    } while (result == -1 && errno == EINTR);
    return result;
}

QString systemError(const QString &prefix)
{
    return QStringLiteral("%1: %2").arg(prefix, QString::fromLocal8Bit(std::strerror(errno)));
}
}

V4L2CaptureSession::V4L2CaptureSession(QObject *parent)
    : QObject(parent)
{
}

V4L2CaptureSession::~V4L2CaptureSession()
{
    stop();
}

void V4L2CaptureSession::start(const QString &devicePath, const CameraFormat &format)
{
    stop();

    if (!format.isValid()) {
        emit errorOccurred(QStringLiteral("Cannot start camera: no valid format selected."));
        return;
    }

    if (!configureDevice(devicePath, format)) {
        cleanupDevice();
        return;
    }

    if (!requestAndMapBuffers()) {
        cleanupDevice();
        return;
    }

    if (!queueAllBuffers()) {
        cleanupDevice();
        return;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fileDescriptor, VIDIOC_STREAMON, &type) == -1) {
        emit errorOccurred(systemError(QStringLiteral("Could not start camera stream")));
        cleanupDevice();
        return;
    }

    m_running = true;
    m_notifier = new QSocketNotifier(m_fileDescriptor, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &V4L2CaptureSession::readAvailableFrames);

    emit streamingStarted();
}

void V4L2CaptureSession::stop()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }

    const bool wasRunning = m_running;

    if (m_fileDescriptor != -1 && m_running) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(m_fileDescriptor, VIDIOC_STREAMOFF, &type);
    }

    m_running = false;
    cleanupDevice();
    m_latestImage = {};

    if (wasRunning) {
        emit streamingStopped();
    }
}

void V4L2CaptureSession::captureCurrentFrame(const QString &filePath)
{
    const QString targetPath = filePath.isEmpty()
        ? ImageCaptureHandler::createDefaultImagePath()
        : filePath;

    QString error;
    if (!ImageCaptureHandler::saveJpeg(m_latestImage, targetPath, &error)) {
        emit errorOccurred(error);
        return;
    }

    emit photoSaved(targetPath);
}

void V4L2CaptureSession::captureHighQualityPhoto(const QString &devicePath, const CameraFormat &format, const QString &filePath, int filterMode)
{
    stop();

    if (!format.isValid()) {
        emit errorOccurred(QStringLiteral("Cannot capture photo: no valid high-quality format selected."));
        return;
    }

    if (!configureDevice(devicePath, format) || !requestAndMapBuffers() || !queueAllBuffers()) {
        cleanupDevice();
        return;
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fileDescriptor, VIDIOC_STREAMON, &type) == -1) {
        emit errorOccurred(systemError(QStringLiteral("Could not start high-quality photo capture")));
        cleanupDevice();
        return;
    }

    QImage image = readStillFrame(5);

    xioctl(m_fileDescriptor, VIDIOC_STREAMOFF, &type);
    cleanupDevice();

    if (image.isNull()) {
        emit errorOccurred(QStringLiteral("Camera did not produce a high-quality photo frame."));
        return;
    }

    image = FrameConverter::applyFilter(image, filterMode);
    const QString targetPath = filePath.isEmpty()
        ? ImageCaptureHandler::createDefaultImagePath()
        : filePath;

    QString error;
    if (!ImageCaptureHandler::saveJpeg(image, targetPath, &error)) {
        emit errorOccurred(error);
        return;
    }

    emit photoSaved(targetPath);
}

void V4L2CaptureSession::readAvailableFrames()
{
    if (m_fileDescriptor == -1 || !m_running) {
        return;
    }

    QImage newestImage;

    for (int frame = 0; frame < maxFramesPerNotifierActivation; ++frame) {
        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;

        if (xioctl(m_fileDescriptor, VIDIOC_DQBUF, &buffer) == -1) {
            if (errno == EAGAIN) {
                break;
            }
            reportErrorAndStop(systemError(QStringLiteral("Could not read camera frame")));
            return;
        }

        if (buffer.index >= quint32(m_buffers.size())) {
            reportErrorAndStop(QStringLiteral("Camera returned an invalid frame buffer index."));
            return;
        }

        const FrameBuffer &mappedBuffer = m_buffers[int(buffer.index)];
        if (buffer.bytesused > 0 && buffer.bytesused <= mappedBuffer.length) {
            const QByteArrayView frameBytes(static_cast<const char *>(mappedBuffer.start), qsizetype(buffer.bytesused));
            QImage image = FrameConverter::toImage(m_activeFormat.pixelFormat, m_activeFormat.width, m_activeFormat.height, frameBytes);
            if (!image.isNull()) {
                newestImage = std::move(image);
            }
        }

        if (xioctl(m_fileDescriptor, VIDIOC_QBUF, &buffer) == -1) {
            reportErrorAndStop(systemError(QStringLiteral("Could not requeue camera frame")));
            return;
        }
    }

    if (!newestImage.isNull()) {
        m_latestImage = newestImage;
        emit previewFrameReady(m_latestImage);
    }
}

bool V4L2CaptureSession::configureDevice(const QString &devicePath, const CameraFormat &format)
{
    m_fileDescriptor = open(devicePath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK, 0);
    if (m_fileDescriptor == -1) {
        emit errorOccurred(systemError(QStringLiteral("Could not open camera %1").arg(devicePath)));
        return false;
    }

    v4l2_format deviceFormat {};
    deviceFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    deviceFormat.fmt.pix.width = quint32(format.width);
    deviceFormat.fmt.pix.height = quint32(format.height);
    deviceFormat.fmt.pix.pixelformat = format.pixelFormat;
    deviceFormat.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(m_fileDescriptor, VIDIOC_S_FMT, &deviceFormat) == -1) {
        emit errorOccurred(systemError(QStringLiteral("Could not set camera format")));
        return false;
    }

    m_activeFormat = format;
    m_activeFormat.width = int(deviceFormat.fmt.pix.width);
    m_activeFormat.height = int(deviceFormat.fmt.pix.height);
    m_activeFormat.pixelFormat = deviceFormat.fmt.pix.pixelformat;
    m_activeFormat.pixelFormatName = v4l2PixelFormatDisplayName(m_activeFormat.pixelFormat);

    if (!FrameConverter::isSupportedPixelFormat(m_activeFormat.pixelFormat)) {
        emit errorOccurred(QStringLiteral("Camera selected an unsupported pixel format: %1").arg(v4l2FourccToString(m_activeFormat.pixelFormat)));
        return false;
    }

    if (format.framesPerSecond > 0) {
        v4l2_streamparm streamParameters {};
        streamParameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(m_fileDescriptor, VIDIOC_G_PARM, &streamParameters) != -1
            && (streamParameters.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
            streamParameters.parm.capture.timeperframe.numerator = 1;
            streamParameters.parm.capture.timeperframe.denominator = quint32(format.framesPerSecond);
            xioctl(m_fileDescriptor, VIDIOC_S_PARM, &streamParameters);
        }
    }

    return true;
}

bool V4L2CaptureSession::requestAndMapBuffers()
{
    v4l2_requestbuffers requestBuffers {};
    requestBuffers.count = requestedBufferCount;
    requestBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requestBuffers.memory = V4L2_MEMORY_MMAP;

    if (xioctl(m_fileDescriptor, VIDIOC_REQBUFS, &requestBuffers) == -1) {
        emit errorOccurred(systemError(QStringLiteral("Could not request camera buffers")));
        return false;
    }

    if (requestBuffers.count < 2) {
        emit errorOccurred(QStringLiteral("Camera did not provide enough streaming buffers."));
        return false;
    }

    m_buffers.resize(int(requestBuffers.count));

    for (quint32 index = 0; index < requestBuffers.count; ++index) {
        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;

        if (xioctl(m_fileDescriptor, VIDIOC_QUERYBUF, &buffer) == -1) {
            emit errorOccurred(systemError(QStringLiteral("Could not query camera buffer")));
            return false;
        }

        void *mappedMemory = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fileDescriptor, buffer.m.offset);
        if (mappedMemory == MAP_FAILED) {
            emit errorOccurred(systemError(QStringLiteral("Could not map camera buffer")));
            return false;
        }

        m_buffers[int(index)] = FrameBuffer{mappedMemory, buffer.length};
    }

    return true;
}

bool V4L2CaptureSession::queueAllBuffers()
{
    for (int index = 0; index < m_buffers.size(); ++index) {
        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = quint32(index);

        if (xioctl(m_fileDescriptor, VIDIOC_QBUF, &buffer) == -1) {
            emit errorOccurred(systemError(QStringLiteral("Could not queue camera buffer")));
            return false;
        }
    }

    return true;
}

QImage V4L2CaptureSession::readStillFrame(int warmupFrames)
{
    QImage newestImage;
    int acceptedFrames = 0;

    while (acceptedFrames <= warmupFrames) {
        pollfd descriptor {};
        descriptor.fd = m_fileDescriptor;
        descriptor.events = POLLIN;

        const int ready = poll(&descriptor, 1, stillCaptureTimeoutMs);
        if (ready <= 0) {
            return newestImage;
        }

        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;

        if (xioctl(m_fileDescriptor, VIDIOC_DQBUF, &buffer) == -1) {
            if (errno == EAGAIN) {
                continue;
            }
            emit errorOccurred(systemError(QStringLiteral("Could not read high-quality photo frame")));
            return {};
        }

        if (buffer.index >= quint32(m_buffers.size())) {
            emit errorOccurred(QStringLiteral("Camera returned an invalid still frame buffer index."));
            return {};
        }

        const FrameBuffer &mappedBuffer = m_buffers[int(buffer.index)];
        if (buffer.bytesused > 0 && buffer.bytesused <= mappedBuffer.length) {
            const QByteArrayView frameBytes(static_cast<const char *>(mappedBuffer.start), qsizetype(buffer.bytesused));
            QImage image = FrameConverter::toImage(m_activeFormat.pixelFormat, m_activeFormat.width, m_activeFormat.height, frameBytes);
            if (!image.isNull()) {
                newestImage = std::move(image);
                ++acceptedFrames;
            }
        }

        if (xioctl(m_fileDescriptor, VIDIOC_QBUF, &buffer) == -1) {
            emit errorOccurred(systemError(QStringLiteral("Could not requeue high-quality photo buffer")));
            return {};
        }
    }

    return newestImage;
}

void V4L2CaptureSession::cleanupDevice()
{
    cleanupBuffers();

    if (m_fileDescriptor != -1) {
        close(m_fileDescriptor);
        m_fileDescriptor = -1;
    }
}

void V4L2CaptureSession::cleanupBuffers()
{
    for (const FrameBuffer &buffer : std::as_const(m_buffers)) {
        if (buffer.isMapped()) {
            munmap(buffer.start, buffer.length);
        }
    }
    m_buffers.clear();
}

void V4L2CaptureSession::reportErrorAndStop(const QString &message)
{
    emit errorOccurred(message);
    stop();
}
