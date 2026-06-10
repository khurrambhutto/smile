#include "CameraManager.h"

#include "FrameConverter.h"
#include "V4L2CaptureSession.h"

#include <QImage>
#include <QMetaObject>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <algorithm>
#include <cstring>

CameraManager::CameraManager(QObject *parent)
    : QObject(parent)
    , m_deviceManager(this)
    , m_settingsStore(this)
    , m_deviceModel(this)
    , m_formatModel(this)
    , m_captureSession(new V4L2CaptureSession)
{
    qRegisterMetaType<QImage>("QImage");

    m_frameClock.start();

    m_captureSession->moveToThread(&m_captureThread);
    connect(&m_captureThread, &QThread::finished, m_captureSession, &QObject::deleteLater);
    connect(m_captureSession, &V4L2CaptureSession::previewFrameReady, this, &CameraManager::presentFrame);
    connect(m_captureSession, &V4L2CaptureSession::errorOccurred, this, &CameraManager::handleError);
    connect(m_captureSession, &V4L2CaptureSession::streamingStarted, this, &CameraManager::handleStreamingStarted);
    connect(m_captureSession, &V4L2CaptureSession::streamingStopped, this, &CameraManager::handleStreamingStopped);
    connect(m_captureSession, &V4L2CaptureSession::photoSaved, this, &CameraManager::handlePhotoSaved);
    connect(&m_recorder, &VideoRecorder::recordingChanged, this, &CameraManager::recordingChanged);
    connect(&m_recorder, &VideoRecorder::errorMessageChanged, this, [this]() {
        if (!m_recorder.errorMessage().isEmpty()) {
            setErrorMessage(m_recorder.errorMessage());
        }
    });

    m_hotplugRefreshTimer.setSingleShot(true);
    m_hotplugRefreshTimer.setInterval(600);
    connect(&m_hotplugRefreshTimer, &QTimer::timeout, this, &CameraManager::refreshDevices);
    if (m_deviceWatcher.addPath(QStringLiteral("/dev"))) {
        connect(&m_deviceWatcher, &QFileSystemWatcher::directoryChanged, this, &CameraManager::scheduleDeviceRefresh);
    }

    m_captureThread.start();

    refreshDevices();
}

CameraManager::~CameraManager()
{
    if (m_captureSession && m_captureThread.isRunning()) {
        QMetaObject::invokeMethod(m_captureSession, &V4L2CaptureSession::stop, Qt::BlockingQueuedConnection);
    }

    m_captureThread.quit();
    m_captureThread.wait();
}

CameraDeviceModel *CameraManager::deviceModel()
{
    return &m_deviceModel;
}

CameraFormatModel *CameraManager::formatModel()
{
    return &m_formatModel;
}

QVideoSink *CameraManager::videoSink() const
{
    return m_videoSink.data();
}

void CameraManager::setVideoSink(QVideoSink *videoSink)
{
    if (m_videoSink == videoSink) {
        return;
    }

    m_videoSink = videoSink;
    emit videoSinkChanged();
}

int CameraManager::selectedDeviceIndex() const
{
    return m_selectedDeviceIndex;
}

void CameraManager::setSelectedDeviceIndex(int index)
{
    if (index < -1 || index >= m_devices.size() || m_selectedDeviceIndex == index) {
        return;
    }

    const bool shouldRestart = m_streaming;
    if (shouldRestart) {
        stop();
    }

    m_selectedDeviceIndex = index;
    emit selectedDeviceIndexChanged();

    updateFormatsForSelectedDevice();

    if (index >= 0) {
        m_settingsStore.setPreferredDevicePath(m_devices.at(index).path);
    }

    if (shouldRestart) {
        start();
    }
}

int CameraManager::selectedFormatIndex() const
{
    return m_selectedFormatIndex;
}

void CameraManager::setSelectedFormatIndex(int index)
{
    if (index < -1 || index >= m_formatModel.formats().size() || m_selectedFormatIndex == index) {
        return;
    }

    const bool shouldRestart = m_streaming;
    if (shouldRestart) {
        stop();
    }

    m_selectedFormatIndex = index;
    emit selectedFormatIndexChanged();

    if (index >= 0) {
        m_settingsStore.setPreferredFormatIndex(index);
    }

    if (shouldRestart) {
        start();
    }
}

bool CameraManager::isStreaming() const
{
    return m_streaming;
}

bool CameraManager::isRecording() const
{
    return m_recorder.isRecording();
}

int CameraManager::captureMode() const
{
    return m_captureMode;
}

void CameraManager::setCaptureMode(int mode)
{
    const int normalizedMode = mode == 1 ? 1 : 0;
    if (m_captureMode == normalizedMode) {
        return;
    }

    m_captureMode = normalizedMode;
    emit captureModeChanged();
}

int CameraManager::filterMode() const
{
    return m_filterMode;
}

void CameraManager::setFilterMode(int mode)
{
    const int normalizedMode = std::clamp(mode, 0, 3);
    if (m_filterMode == normalizedMode) {
        return;
    }

    m_filterMode = normalizedMode;
    emit filterModeChanged();
}

QString CameraManager::errorMessage() const
{
    return m_errorMessage;
}

QString CameraManager::statusMessage() const
{
    return m_statusMessage;
}

QString CameraManager::lastPhotoPath() const
{
    return m_lastPhotoPath;
}

QString CameraManager::lastVideoPath() const
{
    return m_lastVideoPath;
}

void CameraManager::refreshDevices()
{
    const bool shouldRestart = m_streaming;
    if (shouldRestart) {
        stop();
    }

    QString scanError;
    m_devices = m_deviceManager.scanDevices(&scanError);
    m_deviceModel.setDevices(m_devices);

    if (m_devices.isEmpty()) {
        m_selectedDeviceIndex = -1;
        m_selectedFormatIndex = -1;
        m_formatModel.setFormats({});
        emit selectedDeviceIndexChanged();
        emit selectedFormatIndexChanged();
        setErrorMessage(scanError);
        setStatusMessage(QStringLiteral("No camera available"));
        return;
    }

    clearError();

    int preferredIndex = 0;
    const QString preferredPath = m_settingsStore.preferredDevicePath();
    if (!preferredPath.isEmpty()) {
        for (int index = 0; index < m_devices.size(); ++index) {
            if (m_devices.at(index).path == preferredPath) {
                preferredIndex = index;
                break;
            }
        }
    }

    m_selectedDeviceIndex = preferredIndex;
    emit selectedDeviceIndexChanged();
    updateFormatsForSelectedDevice();

    setStatusMessage(QStringLiteral("Found %1 camera%2")
        .arg(m_devices.size())
        .arg(m_devices.size() == 1 ? QString() : QStringLiteral("s")));

    if (shouldRestart) {
        start();
    }
}

void CameraManager::start()
{
    const CameraDevice device = selectedDevice();
    const CameraFormat format = selectedFormat();

    if (!device.isValid()) {
        setErrorMessage(QStringLiteral("No compatible camera selected."));
        return;
    }

    if (!format.isValid()) {
        setErrorMessage(QStringLiteral("No supported camera format selected."));
        return;
    }

    clearError();
    setStatusMessage(QStringLiteral("Starting %1…").arg(device.displayName));

    QMetaObject::invokeMethod(m_captureSession, [session = m_captureSession, path = device.path, format]() {
        session->start(path, format);
    }, Qt::QueuedConnection);
}

void CameraManager::stop()
{
    if (m_recorder.isRecording()) {
        stopRecording();
    }
    QMetaObject::invokeMethod(m_captureSession, &V4L2CaptureSession::stop, Qt::QueuedConnection);
    setStreaming(false);
    setStatusMessage(QStringLiteral("Preview stopped"));
}

void CameraManager::capturePhoto()
{
    if (!m_streaming) {
        setErrorMessage(QStringLiteral("Start the camera before taking a photo."));
        return;
    }

    const CameraDevice device = selectedDevice();
    const CameraFormat previewFormat = selectedFormat();
    const CameraFormat photoFormat = bestPhotoFormat();
    if (!device.isValid() || !photoFormat.isValid()) {
        setErrorMessage(QStringLiteral("No high-quality photo format is available."));
        return;
    }

    setStatusMessage(QStringLiteral("Capturing full-quality photo…"));
    QMetaObject::invokeMethod(m_captureSession, [session = m_captureSession, path = device.path, photoFormat, previewFormat, filterMode = m_filterMode]() {
        session->captureHighQualityPhoto(path, photoFormat, QString(), filterMode);
        if (previewFormat.isValid()) {
            session->start(path, previewFormat);
        }
    }, Qt::QueuedConnection);
}

void CameraManager::toggleRecording()
{
    if (m_recorder.isRecording()) {
        stopRecording();
    } else {
        startRecording();
    }
}

void CameraManager::startRecording()
{
    if (!m_streaming) {
        setErrorMessage(QStringLiteral("Start the camera before recording video."));
        return;
    }

    const CameraFormat format = selectedFormat();
    EncoderSettings settings;
    settings.width = format.width;
    settings.height = format.height;
    settings.framesPerSecond = std::max(1, format.framesPerSecond);

    QString error;
    if (!m_recorder.start(settings, &error)) {
        setErrorMessage(error);
        return;
    }

    m_lastVideoPath = m_recorder.outputPath();
    emit lastVideoPathChanged();
    setCaptureMode(1);
    setStatusMessage(QStringLiteral("Recording video…"));
}

void CameraManager::stopRecording()
{
    QString error;
    const QString outputPath = m_recorder.outputPath();
    if (!m_recorder.stop(&error)) {
        setErrorMessage(error);
        return;
    }

    if (!outputPath.isEmpty()) {
        m_lastVideoPath = outputPath;
        emit lastVideoPathChanged();
        setStatusMessage(QStringLiteral("Video saved: %1").arg(outputPath));
    }
}

void CameraManager::presentFrame(const QImage &image)
{
    if (image.isNull() || m_videoSink.isNull()) {
        return;
    }

    const QImage filteredImage = FrameConverter::applyFilter(image, m_filterMode);
    m_recorder.writeFrame(filteredImage);

    const QImage rgbaImage = filteredImage.convertToFormat(QImage::Format_RGBA8888);
    QVideoFrameFormat frameFormat(rgbaImage.size(), QVideoFrameFormat::Format_RGBA8888);
    frameFormat.setScanLineDirection(QVideoFrameFormat::TopToBottom);

    QVideoFrame frame(frameFormat);
    if (!frame.isValid() || !frame.map(QVideoFrame::WriteOnly)) {
        return;
    }

    const int destinationStride = frame.bytesPerLine(0);
    const int sourceStride = rgbaImage.bytesPerLine();
    const int bytesPerRow = std::min(destinationStride, sourceStride);
    uchar *destination = frame.bits(0);
    const uchar *source = rgbaImage.constBits();

    for (int row = 0; row < rgbaImage.height(); ++row) {
        std::memcpy(destination + row * destinationStride, source + row * sourceStride, std::size_t(bytesPerRow));
    }

    frame.unmap();
    frame.setStartTime(m_frameClock.nsecsElapsed() / 1000);
    m_videoSink->setVideoFrame(frame);
}

void CameraManager::handleError(const QString &message)
{
    setErrorMessage(message);
    setStatusMessage(QStringLiteral("Camera error"));
    setStreaming(false);
}

void CameraManager::handleStreamingStarted()
{
    setStreaming(true);
    setStatusMessage(QStringLiteral("Preview running"));
}

void CameraManager::handleStreamingStopped()
{
    setStreaming(false);
}

void CameraManager::handlePhotoSaved(const QString &filePath)
{
    if (m_lastPhotoPath == filePath) {
        return;
    }

    m_lastPhotoPath = filePath;
    emit lastPhotoPathChanged();
    setStatusMessage(QStringLiteral("Photo saved: %1").arg(filePath));
}

void CameraManager::scheduleDeviceRefresh()
{
    m_hotplugRefreshTimer.start();
}

CameraDevice CameraManager::selectedDevice() const
{
    if (m_selectedDeviceIndex < 0 || m_selectedDeviceIndex >= m_devices.size()) {
        return {};
    }
    return m_devices.at(m_selectedDeviceIndex);
}

CameraFormat CameraManager::selectedFormat() const
{
    return m_formatModel.formatAt(m_selectedFormatIndex);
}

CameraFormat CameraManager::bestPhotoFormat() const
{
    if (m_selectedDeviceIndex < 0 || m_selectedDeviceIndex >= m_devices.size()) {
        return {};
    }

    const QList<CameraFormat> formats = m_devices.at(m_selectedDeviceIndex).formats;
    if (formats.isEmpty()) {
        return {};
    }

    return *std::max_element(formats.begin(), formats.end(), [](const CameraFormat &left, const CameraFormat &right) {
        const qint64 leftPixels = qint64(left.width) * qint64(left.height);
        const qint64 rightPixels = qint64(right.width) * qint64(right.height);
        if (leftPixels != rightPixels) {
            return leftPixels < rightPixels;
        }
        return left.preferenceScore() < right.preferenceScore();
    });
}

void CameraManager::updateFormatsForSelectedDevice()
{
    if (m_selectedDeviceIndex < 0 || m_selectedDeviceIndex >= m_devices.size()) {
        m_formatModel.setFormats({});
        m_selectedFormatIndex = -1;
        emit selectedFormatIndexChanged();
        return;
    }

    const QList<CameraFormat> formats = m_devices.at(m_selectedDeviceIndex).formats;
    m_formatModel.setFormats(formats);

    int preferredFormat = m_settingsStore.preferredFormatIndex();
    if (preferredFormat < 0 || preferredFormat >= formats.size()) {
        preferredFormat = formats.isEmpty() ? -1 : 0;
    }

    if (m_selectedFormatIndex != preferredFormat) {
        m_selectedFormatIndex = preferredFormat;
        emit selectedFormatIndexChanged();
    }
}

void CameraManager::setStreaming(bool streaming)
{
    if (m_streaming == streaming) {
        return;
    }

    m_streaming = streaming;
    emit streamingChanged();
}

void CameraManager::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

void CameraManager::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }

    m_statusMessage = message;
    emit statusMessageChanged();
}

void CameraManager::clearError()
{
    setErrorMessage(QString());
}
