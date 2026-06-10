#include "CameraManager.h"

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

    QMetaObject::invokeMethod(m_captureSession, [session = m_captureSession]() {
        session->captureCurrentFrame(QString());
    }, Qt::QueuedConnection);
}

void CameraManager::presentFrame(const QImage &image)
{
    if (image.isNull() || m_videoSink.isNull()) {
        return;
    }

    const QImage rgbaImage = image.convertToFormat(QImage::Format_RGBA8888);
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
