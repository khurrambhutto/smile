#pragma once

#include "DeviceManager.h"
#include "app/SettingsStore.h"
#include "capture/VideoRecorder.h"
#include "models/CameraDeviceModel.h"
#include "models/CameraFormatModel.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QVideoSink>

class V4L2CaptureSession;

class CameraManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(CameraDeviceModel *deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(CameraFormatModel *formatModel READ formatModel CONSTANT)
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(int selectedDeviceIndex READ selectedDeviceIndex WRITE setSelectedDeviceIndex NOTIFY selectedDeviceIndexChanged)
    Q_PROPERTY(int selectedFormatIndex READ selectedFormatIndex WRITE setSelectedFormatIndex NOTIFY selectedFormatIndexChanged)
    Q_PROPERTY(bool streaming READ isStreaming NOTIFY streamingChanged)
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(int captureMode READ captureMode WRITE setCaptureMode NOTIFY captureModeChanged)
    Q_PROPERTY(int filterMode READ filterMode WRITE setFilterMode NOTIFY filterModeChanged)
    Q_PROPERTY(QString recordingDurationText READ recordingDurationText NOTIFY recordingDurationTextChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString lastPhotoPath READ lastPhotoPath NOTIFY lastPhotoPathChanged)
    Q_PROPERTY(QString lastVideoPath READ lastVideoPath NOTIFY lastVideoPathChanged)

public:
    explicit CameraManager(QObject *parent = nullptr);
    ~CameraManager() override;

    CameraDeviceModel *deviceModel();
    CameraFormatModel *formatModel();

    QVideoSink *videoSink() const;
    void setVideoSink(QVideoSink *videoSink);

    int selectedDeviceIndex() const;
    void setSelectedDeviceIndex(int index);

    int selectedFormatIndex() const;
    void setSelectedFormatIndex(int index);

    bool isStreaming() const;
    bool isRecording() const;
    int captureMode() const;
    void setCaptureMode(int mode);
    int filterMode() const;
    void setFilterMode(int mode);
    QString recordingDurationText() const;
    QString errorMessage() const;
    QString statusMessage() const;
    QString lastPhotoPath() const;
    QString lastVideoPath() const;

    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void capturePhoto();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

signals:
    void videoSinkChanged();
    void selectedDeviceIndexChanged();
    void selectedFormatIndexChanged();
    void streamingChanged();
    void recordingChanged();
    void captureModeChanged();
    void filterModeChanged();
    void recordingDurationTextChanged();
    void errorMessageChanged();
    void statusMessageChanged();
    void lastPhotoPathChanged();
    void lastVideoPathChanged();

private slots:
    void presentFrame(const QImage &image);
    void handleError(const QString &message);
    void handleStreamingStarted();
    void handleStreamingStopped();
    void handlePhotoSaved(const QString &filePath);
    void scheduleDeviceRefresh();
    void updateRecordingDuration();

private:
    CameraDevice selectedDevice() const;
    CameraFormat selectedFormat() const;
    CameraFormat bestPhotoFormat() const;
    void updateFormatsForSelectedDevice();
    void setStreaming(bool streaming);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    void clearError();

    DeviceManager m_deviceManager;
    SettingsStore m_settingsStore;
    CameraDeviceModel m_deviceModel;
    CameraFormatModel m_formatModel;
    VideoRecorder m_recorder;
    QList<CameraDevice> m_devices;

    QThread m_captureThread;
    V4L2CaptureSession *m_captureSession = nullptr;
    QPointer<QVideoSink> m_videoSink;
    QElapsedTimer m_frameClock;
    QElapsedTimer m_recordingClock;
    QFileSystemWatcher m_deviceWatcher;
    QTimer m_hotplugRefreshTimer;
    QTimer m_recordingDurationTimer;

    int m_selectedDeviceIndex = -1;
    int m_selectedFormatIndex = -1;
    int m_captureMode = 0;
    int m_filterMode = 0;
    bool m_streaming = false;
    QString m_errorMessage;
    QString m_statusMessage;
    QString m_lastPhotoPath;
    QString m_lastVideoPath;
    QString m_recordingDurationText = QStringLiteral("00:00");
};
