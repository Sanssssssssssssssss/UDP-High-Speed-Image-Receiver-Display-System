#ifndef UDP_FRAME_PIPELINE_WORKER_H
#define UDP_FRAME_PIPELINE_WORKER_H

#include "UdpReceiver.h"
#include "VideoRecorderWorker.h"
#include "YoloProcessor.h"
#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <array>

class UdpFramePipelineWorker : public QObject {
    Q_OBJECT

public:
    explicit UdpFramePipelineWorker(QObject *parent = nullptr);
    ~UdpFramePipelineWorker();

signals:
    void frameReady(const QImage &frame);
    void recordingStateChanged(bool isRecording);
    void statsReady(const QString &statsText);
    void receiverStatusChanged(const QString &statusText);
    void receiverSettingsChanged(const QString &address, quint16 port);
    void aiStatusChanged(const QString &statusText);
    void recordFrameReady(const QImage &frame);
    void startRecordingRequested(const QString &directory, const QString &format, int fps, const QSize &frameSize);
    void stopRecordingRequested();

public slots:
    void start();
    void shutdown();
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

private slots:
    void updateStats();
    void enqueueFrameBatch(const QList<QByteArray> &batch);
    void drainPendingBatches();
    void onReceiverBindingChanged(const QString &address, quint16 port, bool ok, const QString &message);
    void onYoloDetectionsReady(const QVector<QRect> &boxes, int inferenceMs);
    void onYoloStatusChanged(const QString &statusText);

private:
    static constexpr int kFrameWidth = 400;
    static constexpr int kFrameHeight = 400;
    static constexpr int kPacketHeaderSize = 4;
    static constexpr int kLinePayloadBytes = kFrameWidth * 2;
    static constexpr int kPacketsPerFrame = kFrameHeight + 2;
    static constexpr int kMaxQueuedFrames = 6;
    static constexpr int kMaxQueuedPackets = kPacketsPerFrame * kMaxQueuedFrames;

    static bool isMarkerPacket(const QByteArray &data, char marker);
    void resetParserState(bool clearPendingQueue);
    void processFrameData(const QByteArray &data);
    void finalizeFrame();
    void composeDisplayFrame(bool submitAiFrame = true);
    void copyRawToDisplay(QImage &dest);
    void flipImageInPlace(QImage &image) const;
    void drawDetections(QImage &image) const;
    QRect transformDetectionRect(const QRect &rect) const;
    void refreshDisplayFromRaw();
    void rebuildToneLutIfNeeded();
    void applyProcessing(const cv::Mat &sourceRgb, cv::Mat &destRgb);
    void averageLineBytes(char *destLine, const char *topLine, const char *bottomLine, int length) const;

    UdpReceiver *receiver;
    QThread *receiverThread;

    YoloProcessor *yoloProcessor;
    QThread *yoloThread;

    VideoRecorderWorker *recorderWorker;
    QThread *recorderThread;

    QTimer *statsTimer;

    bool flipHorizontal;
    bool flipVertical;
    int brightnessValue;
    int gammaValue;
    int sharpnessValue;
    int denoiseValue;
    bool lutDirty;
    std::array<uchar, 256> toneLut;

    QString receiverAddress;
    quint16 receiverPort;
    bool aiDetectionEnabled;
    QString aiStatusText;
    int lastInferenceMs;
    QVector<QRect> latestDetections;
    bool isRecording;

    int currentLine;
    bool frameValid;
    QByteArray frameData;
    QVector<int> linePayloadSizes;
    QVector<uchar> receivedLineFlags;
    QQueue<QList<QByteArray> > pendingBatches;
    QMutex pendingBatchMutex;
    int pendingPacketCount;
    bool drainScheduled;
    bool parserResyncPending;

    QImage rawImage;
    QImage displayBuffers[2];
    int frontDisplayIndex;

    quint64 datagramsThisSecond;
    quint64 completedFramesThisSecond;
    quint64 recoveredLinesThisSecond;
    quint64 frameProcessingNsThisSecond;
    quint64 interpolationNsThisSecond;
    quint64 droppedPacketsThisSecond;
    quint64 droppedBatchesThisSecond;
    quint64 maxQueuedPacketsThisSecond;
    quint64 drainNsThisSecond;
    quint64 maxDrainNsThisSecond;
    quint64 startMarkersThisSecond;
    quint64 endMarkersThisSecond;
    quint64 startWithoutEndThisSecond;
    quint64 endWithoutStartThisSecond;
    quint64 orphanLinePacketsThisSecond;
    quint64 overflowLinePacketsThisSecond;
    quint64 shortFrameEndsThisSecond;
    quint64 parserResyncEventsThisSecond;
};

#endif // UDP_FRAME_PIPELINE_WORKER_H
