#pragma once

#include <QString>

struct EncoderSettings
{
    int width = 1280;
    int height = 720;
    int framesPerSecond = 30;
    int videoBitrate = 10'000'000;
    int jpegQuality = 92;
    int crf = 20;
    QString videoCodec = QStringLiteral("libx264");
    QString audioCodec = QStringLiteral("aac");
    QString audioInput = QStringLiteral("default");
};
