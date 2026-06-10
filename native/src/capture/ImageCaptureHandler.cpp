#include "ImageCaptureHandler.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

QString ImageCaptureHandler::defaultCaptureDirectory()
{
    const QString picturesLocation = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString baseDirectory = picturesLocation.isEmpty()
        ? QDir::homePath()
        : picturesLocation;

    return QDir(baseDirectory).filePath(QStringLiteral("Smile"));
}

QString ImageCaptureHandler::createDefaultImagePath()
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"));
    return QDir(defaultCaptureDirectory()).filePath(QStringLiteral("Smile-%1.jpg").arg(timestamp));
}

bool ImageCaptureHandler::saveJpeg(const QImage &image, const QString &filePath, QString *errorMessage)
{
    if (image.isNull()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No camera frame is ready to capture yet.");
        }
        return false;
    }

    QFileInfo fileInfo(filePath);
    QDir directory(fileInfo.absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not create capture directory: %1").arg(directory.absolutePath());
        }
        return false;
    }

    if (!image.save(filePath, "JPG", 95)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not save photo: %1").arg(filePath);
        }
        return false;
    }

    return true;
}
