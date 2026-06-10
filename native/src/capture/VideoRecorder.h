#pragma once

#include "EncoderSettings.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QString>

class VideoRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputPathChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit VideoRecorder(QObject *parent = nullptr);

    bool isRecording() const;
    QString outputPath() const;
    QString errorMessage() const;

    bool start(const EncoderSettings &settings, QString *errorMessage = nullptr);
    bool stop(QString *errorMessage = nullptr);
    void writeFrame(const QImage &image);

    static QString defaultRecordingDirectory();
    static QString createDefaultVideoPath();

signals:
    void recordingChanged();
    void outputPathChanged();
    void errorMessageChanged();

private:
    void setRecording(bool recording);
    void setOutputPath(const QString &outputPath);
    void setErrorMessage(const QString &message);
    QStringList buildFfmpegArguments(const EncoderSettings &settings) const;

    QProcess m_process;
    EncoderSettings m_settings;
    QElapsedTimer m_frameClock;
    qint64 m_lastFrameTimeMs = -1;
    bool m_recording = false;
    QString m_outputPath;
    QString m_errorMessage;
};
