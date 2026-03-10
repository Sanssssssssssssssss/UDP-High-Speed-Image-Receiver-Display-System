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
    void applyReceiverSettings(const QString &address, quint16 port);
    void setAiDetectionEnabled(bool enabled);

signals:
    void recordingStateChanged(bool isRecording);
    void fpsChanged(int fps);
    void performanceStatsChanged(const QString &statsText);
    void receiverStatusChanged(const QString &statusText);
    void receiverSettingsChanged(const QString &address, quint16 port);
    void aiStatusChanged(const QString &statusText);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateFPS();
    void enqueueFrameBatch(const QList<QByteArray> &batch);
    void drainPendingBatches();
    void onReceiverBindingChanged(const QString &address, quint16 port, bool ok, const QString &message);
    void presentLatestFrame();

private:
    static bool isMarkerPacket(const QByteArray &data, char marker);
    void processFrameData(const QByteArray &data);
    void finalizeFrame();
    void writeFrameToVideo();
    void refreshDisplayImage();
    void applyProcessing(const cv::Mat &sourceRgb, cv::Mat &destRgb) const;
    QImage buildOutputFrame(const QImage &sourceFrame) const;
    void resetParserState();

    QImage rawImage;
    QImage displayImage;
    QMutex imageMutex;

    QTimer *fpsTimer;
    QTimer *presentTimer;
    int frameCount;
    int presentedFrameCount;
    quint64 datagramsThisSecond;
    quint64 completedFramesThisSecond;
    quint64 recoveredLinesThisSecond;
    quint64 frameProcessingNsThisSecond;
    quint64 interpolationNsThisSecond;
    quint64 droppedPacketsThisSecond;
    quint64 droppedBatchesThisSecond;
    quint64 maxQueuedPacketsThisSecond;
    quint64 startMarkersThisSecond;
    quint64 endMarkersThisSecond;
    quint64 startWithoutEndThisSecond;
    quint64 endWithoutStartThisSecond;
    quint64 orphanLinePacketsThisSecond;
    quint64 overflowLinePacketsThisSecond;
    quint64 shortFrameEndsThisSecond;
    quint64 parserResyncEventsThisSecond;
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
    QString receiverAddress;
    quint16 receiverPort;
    bool aiDetectionEnabled;
    cv::VideoWriter videoWriter;
    bool isRecording;
    bool framePendingPresentation;
};

#endif // UDP_FRAME_PROCESSOR_H
