#ifndef UDP_FRAME_PROCESSOR_H
#define UDP_FRAME_PROCESSOR_H

#include "UdpReceiver.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QQueue>
#include <QImage>
#include <QList>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <opencv2/opencv.hpp>

class UdpFrameProcessor : public QWidget {
    Q_OBJECT

public:
    explicit UdpFrameProcessor(QWidget *parent = nullptr);
    ~UdpFrameProcessor();

    QImage getCurrentFrame();

public slots:
    void saveSnapshot(const QString &directory);
    void toggleRecording(const QString &directory, const QString &format, int fps = 30);
    void setFlipHorizontal(bool enabled);
    void setFlipVertical(bool enabled);
    void setBrightness(int value);
    void setGamma(int value);
    void setSharpness(int value);
    void setDenoise(int value);

signals:
    void recordingStateChanged(bool isRecording);
    void fpsChanged(int fps);
    void performanceStatsChanged(const QString &statsText);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateFPS();
    void enqueueFrameBatch(const QList<QByteArray> &batch);
    void drainPendingBatches();

private:
    static bool isMarkerPacket(const QByteArray &data, char marker);
    void processFrameData(const QByteArray &data);
    void finalizeFrame();
    void writeFrameToVideo();
    void refreshDisplayImage();
    void applyProcessing(const cv::Mat &sourceRgb, cv::Mat &destRgb) const;
    QImage buildOutputFrame(const QImage &sourceFrame) const;

    QImage rawImage;
    QImage displayImage;
    QMutex imageMutex;

    QTimer *fpsTimer;
    int frameCount;
    quint64 datagramsThisSecond;
    quint64 completedFramesThisSecond;
    quint64 recoveredLinesThisSecond;
    quint64 frameProcessingNsThisSecond;
    quint64 interpolationNsThisSecond;
    quint64 droppedPacketsThisSecond;
    quint64 droppedBatchesThisSecond;
    quint64 maxQueuedPacketsThisSecond;
    int currentLine;
    bool frameValid;
    QVector<QByteArray> frameBuffer;
    QVector<bool> receivedLineFlags;
    QQueue<QList<QByteArray> > pendingBatches;
    QMutex pendingBatchMutex;
    int pendingPacketCount;
    bool drainScheduled;
    bool parserResyncPending;

    UdpReceiver *receiver;
    QThread *receiverThread;

    bool flipHorizontal;
    bool flipVertical;
    int brightnessValue;
    int gammaValue;
    int sharpnessValue;
    int denoiseValue;
    cv::VideoWriter videoWriter;
    bool isRecording;
};

#endif // UDP_FRAME_PROCESSOR_H
