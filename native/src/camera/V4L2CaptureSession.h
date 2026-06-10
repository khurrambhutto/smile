#pragma once

#include "CameraFormat.h"
#include "FrameBuffer.h"

#include <QImage>
#include <QObject>
#include <QVector>

class QSocketNotifier;

class V4L2CaptureSession : public QObject
{
    Q_OBJECT

public:
    explicit V4L2CaptureSession(QObject *parent = nullptr);
    ~V4L2CaptureSession() override;

public slots:
    void start(const QString &devicePath, const CameraFormat &format);
    void stop();
    void captureCurrentFrame(const QString &filePath);

signals:
    void previewFrameReady(const QImage &image);
    void photoSaved(const QString &filePath);
    void errorOccurred(const QString &message);
    void streamingStarted();
    void streamingStopped();

private slots:
    void readAvailableFrames();

private:
    bool configureDevice(const QString &devicePath, const CameraFormat &format);
    bool requestAndMapBuffers();
    bool queueAllBuffers();
    void cleanupDevice();
    void cleanupBuffers();
    void reportErrorAndStop(const QString &message);

    int m_fileDescriptor = -1;
    QVector<FrameBuffer> m_buffers;
    QSocketNotifier *m_notifier = nullptr;
    CameraFormat m_activeFormat;
    QImage m_latestImage;
    bool m_running = false;
};
