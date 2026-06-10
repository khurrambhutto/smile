#include "VideoRecorder.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

VideoRecorder::VideoRecorder(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        Q_UNUSED(error);
        setErrorMessage(QStringLiteral("Recording process error: %1").arg(m_process.errorString()));
        setRecording(false);
    });

    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            const QString details = QString::fromLocal8Bit(m_process.readAllStandardError()).trimmed();
            setErrorMessage(details.isEmpty()
                ? QStringLiteral("Recording stopped unexpectedly.")
                : QStringLiteral("Recording failed: %1").arg(details));
        }
        setRecording(false);
    });
}

bool VideoRecorder::isRecording() const
{
    return m_recording;
}

QString VideoRecorder::outputPath() const
{
    return m_outputPath;
}

QString VideoRecorder::errorMessage() const
{
    return m_errorMessage;
}

bool VideoRecorder::start(const EncoderSettings &settings, QString *errorMessage)
{
    if (m_recording) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Recording is already active.");
        }
        return false;
    }

    m_settings = settings;
    setOutputPath(createDefaultVideoPath());

    const QString ffmpegPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpegPath.isEmpty()) {
        const QString message = QStringLiteral("Could not find ffmpeg. Install ffmpeg to record MP4 video.");
        setErrorMessage(message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    const QFileInfo outputInfo(m_outputPath);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        const QString message = QStringLiteral("Could not create recording directory: %1").arg(outputInfo.absolutePath());
        setErrorMessage(message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    m_process.setProgram(ffmpegPath);
    m_process.setArguments(buildFfmpegArguments(settings));
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start(QIODevice::WriteOnly);

    if (!m_process.waitForStarted(2500)) {
        const QString message = QStringLiteral("Could not start ffmpeg for MP4 recording: %1").arg(m_process.errorString());
        setErrorMessage(message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    if (m_process.waitForFinished(350)) {
        const QString details = QString::fromLocal8Bit(m_process.readAllStandardError()).trimmed();
        const QString message = details.isEmpty()
            ? QStringLiteral("ffmpeg ended before recording could start.")
            : QStringLiteral("ffmpeg could not start recording: %1").arg(details);
        setErrorMessage(message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    m_frameClock.restart();
    m_lastFrameTimeMs = -1;
    setErrorMessage(QString());
    setRecording(true);
    return true;
}

bool VideoRecorder::stop(QString *errorMessage)
{
    if (!m_recording) {
        return true;
    }

    m_process.closeWriteChannel();
    if (!m_process.waitForFinished(5000)) {
        m_process.kill();
        m_process.waitForFinished(1500);
    }

    const bool ok = m_process.exitStatus() == QProcess::NormalExit && m_process.exitCode() == 0;
    if (!ok) {
        const QString details = QString::fromLocal8Bit(m_process.readAllStandardError()).trimmed();
        const QString message = details.isEmpty()
            ? QStringLiteral("ffmpeg failed to finalize the recording.")
            : QStringLiteral("ffmpeg failed to finalize the recording: %1").arg(details);
        setErrorMessage(message);
        if (errorMessage) {
            *errorMessage = message;
        }
    }

    setRecording(false);
    return ok;
}

void VideoRecorder::writeFrame(const QImage &image)
{
    if (!m_recording || image.isNull() || m_process.state() != QProcess::Running) {
        return;
    }

    const qint64 now = m_frameClock.elapsed();
    const qint64 frameIntervalMs = 1000 / std::max(1, m_settings.framesPerSecond);
    if (m_lastFrameTimeMs >= 0 && now - m_lastFrameTimeMs < frameIntervalMs) {
        return;
    }

    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPG", m_settings.jpegQuality)) {
        setErrorMessage(QStringLiteral("Could not encode a video frame."));
        return;
    }

    if (m_process.write(jpeg) == -1) {
        setErrorMessage(QStringLiteral("Could not write a video frame to ffmpeg."));
        return;
    }

    m_lastFrameTimeMs = now;
}

QString VideoRecorder::defaultRecordingDirectory()
{
    const QString videosLocation = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString baseDirectory = videosLocation.isEmpty()
        ? QDir::homePath()
        : videosLocation;

    return QDir(baseDirectory).filePath(QStringLiteral("Smile"));
}

QString VideoRecorder::createDefaultVideoPath()
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"));
    return QDir(defaultRecordingDirectory()).filePath(QStringLiteral("Smile-%1.mp4").arg(timestamp));
}

void VideoRecorder::setRecording(bool recording)
{
    if (m_recording == recording) {
        return;
    }

    m_recording = recording;
    emit recordingChanged();
}

void VideoRecorder::setOutputPath(const QString &outputPath)
{
    if (m_outputPath == outputPath) {
        return;
    }

    m_outputPath = outputPath;
    emit outputPathChanged();
}

void VideoRecorder::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

QStringList VideoRecorder::buildFfmpegArguments(const EncoderSettings &settings) const
{
    return {
        QStringLiteral("-y"),
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-f"),
        QStringLiteral("image2pipe"),
        QStringLiteral("-vcodec"),
        QStringLiteral("mjpeg"),
        QStringLiteral("-framerate"),
        QString::number(std::max(1, settings.framesPerSecond)),
        QStringLiteral("-i"),
        QStringLiteral("-"),
        QStringLiteral("-thread_queue_size"),
        QStringLiteral("512"),
        QStringLiteral("-f"),
        QStringLiteral("pulse"),
        QStringLiteral("-i"),
        settings.audioInput,
        QStringLiteral("-fflags"),
        QStringLiteral("+genpts"),
        QStringLiteral("-c:v"),
        settings.videoCodec,
        QStringLiteral("-preset"),
        QStringLiteral("veryfast"),
        QStringLiteral("-crf"),
        QString::number(settings.crf),
        QStringLiteral("-b:v"),
        QString::number(std::max(1, settings.videoBitrate)),
        QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"),
        settings.audioCodec,
        QStringLiteral("-b:a"),
        QStringLiteral("160k"),
        QStringLiteral("-shortest"),
        m_outputPath
    };
}
