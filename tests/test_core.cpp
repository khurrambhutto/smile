#include "camera/CameraFormat.h"
#include "camera/FrameConverter.h"

#include <QImage>
#include <QtTest/QtTest>
#include <linux/videodev2.h>

class SmileCoreTests : public QObject
{
    Q_OBJECT

private slots:
    void formatPreferenceRanksMjpegAboveYuyv();
    void filtersReturnValidImages();
};

void SmileCoreTests::formatPreferenceRanksMjpegAboveYuyv()
{
    const CameraFormat mjpeg {
        V4L2_PIX_FMT_MJPEG,
        QStringLiteral("MJPEG"),
        1280,
        720,
        30,
    };
    const CameraFormat yuyv {
        V4L2_PIX_FMT_YUYV,
        QStringLiteral("YUYV"),
        1280,
        720,
        30,
    };

    QVERIFY(mjpeg.isValid());
    QVERIFY(yuyv.isValid());
    QVERIFY(mjpeg.preferenceScore() > yuyv.preferenceScore());
}

void SmileCoreTests::filtersReturnValidImages()
{
    QImage image(96, 72, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            row[x] = qRgb((x * 255) / image.width(), (y * 255) / image.height(), 128);
        }
    }

    for (int mode = FrameConverter::Natural; mode <= FrameConverter::Comic; ++mode) {
        const QImage filtered = FrameConverter::applyFilter(image, mode);
        QVERIFY(!filtered.isNull());
        QCOMPARE(filtered.size(), image.size());
    }
}

QTEST_MAIN(SmileCoreTests)

#include "test_core.moc"
