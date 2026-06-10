#include "camera/DeviceManager.h"
#include "camera/FrameConverter.h"
#include "camera/V4L2CaptureSession.h"
#include "capture/VideoRecorder.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <algorithm>
#include <iostream>

namespace {
CameraFormat bestPhotoFormat(const CameraDevice &device)
{
    if (device.formats.isEmpty()) {
        return {};
    }

    return *std::max_element(device.formats.begin(), device.formats.end(), [](const CameraFormat &left, const CameraFormat &right) {
        const qint64 leftPixels = qint64(left.width) * qint64(left.height);
        const qint64 rightPixels = qint64(right.width) * qint64(right.height);
        if (leftPixels != rightPixels) {
            return leftPixels < rightPixels;
        }
        return left.preferenceScore() < right.preferenceScore();
    });
}

}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::cerr << "Could not create a temporary smoke-test directory.\n";
        return 1;
    }

    QString scanError;
    DeviceManager deviceManager;
    const QList<CameraDevice> devices = deviceManager.scanDevices(&scanError);
    if (devices.isEmpty()) {
        std::cerr << "No compatible V4L2 camera found: " << scanError.toStdString() << "\n";
        return 1;
    }

    const CameraDevice device = devices.first();
    const CameraFormat photoFormat = bestPhotoFormat(device);
    if (!device.isValid() || !photoFormat.isValid()) {
        std::cerr << "Camera has no supported MJPEG/YUYV photo format.\n";
        return 1;
    }

    V4L2CaptureSession captureSession;
    QString photoPath;
    QString cameraError;
    QObject::connect(&captureSession, &V4L2CaptureSession::photoSaved, &app, [&](const QString &path) {
        photoPath = path;
    });
    QObject::connect(&captureSession, &V4L2CaptureSession::errorOccurred, &app, [&](const QString &message) {
        cameraError = message;
    });

    const QString requestedPhotoPath = QDir(tempDir.path()).filePath(QStringLiteral("smoke-photo.jpg"));
    captureSession.captureHighQualityPhoto(device.path, photoFormat, requestedPhotoPath, FrameConverter::Natural);
    if (photoPath.isEmpty()) {
        std::cerr << "High-quality photo capture failed: " << cameraError.toStdString() << "\n";
        return 1;
    }

    const QImage captured(photoPath);
    if (captured.isNull() || captured.width() <= 0 || captured.height() <= 0) {
        std::cerr << "Captured photo is not a valid image: " << photoPath.toStdString() << "\n";
        return 1;
    }

    const CameraFormat previewFormat = device.formats.first();

    EncoderSettings settings;
    settings.width = previewFormat.width;
    settings.height = previewFormat.height;
    settings.framesPerSecond = std::clamp(previewFormat.framesPerSecond, 1, 30);
    settings.videoBitrate = 2'000'000;
    settings.crf = 24;
    settings.jpegQuality = 88;

    VideoRecorder recorder;
    QString recordingError;
    if (!recorder.start(settings, &recordingError)) {
        std::cerr << "Video recorder failed to start: " << recordingError.toStdString() << "\n";
        return 1;
    }

    int recordedFrames = 0;
    QEventLoop recordLoop;
    QObject::connect(&captureSession, &V4L2CaptureSession::previewFrameReady, &app, [&](const QImage &frame) {
        recorder.writeFrame(frame);
        ++recordedFrames;
    });
    QObject::connect(&captureSession, &V4L2CaptureSession::errorOccurred, &app, [&](const QString &message) {
        cameraError = message;
        recordLoop.quit();
    });

    captureSession.start(device.path, previewFormat);
    QTimer::singleShot(2200, &recordLoop, &QEventLoop::quit);
    recordLoop.exec();
    captureSession.stop();

    if (recordedFrames <= 0) {
        recorder.stop();
        std::cerr << "Camera preview produced no frames for video recording: " << cameraError.toStdString() << "\n";
        return 1;
    }

    if (!recorder.stop(&recordingError)) {
        std::cerr << "Video recorder failed to stop: " << recordingError.toStdString() << "\n";
        return 1;
    }

    const QFileInfo videoInfo(recorder.outputPath());
    if (!videoInfo.exists() || videoInfo.size() <= 0) {
        std::cerr << "Recorded video was not written: " << recorder.outputPath().toStdString() << "\n";
        return 1;
    }

    std::cout << "Captured photo: " << photoPath.toStdString()
              << " (" << captured.width() << "x" << captured.height() << ")\n";
    std::cout << "Recorded video: " << recorder.outputPath().toStdString()
              << " (" << videoInfo.size() << " bytes, " << recordedFrames << " camera frames)\n";

    return 0;
}
