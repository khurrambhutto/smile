#pragma once

#include <QImage>
#include <QString>

class ImageCaptureHandler
{
public:
    static QString defaultCaptureDirectory();
    static QString createDefaultImagePath();
    static bool saveJpeg(const QImage &image, const QString &filePath, QString *errorMessage = nullptr);
};
