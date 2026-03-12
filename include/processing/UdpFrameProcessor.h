#ifndef UDP_FRAME_PROCESSOR_H
#define UDP_FRAME_PROCESSOR_H

#include <QImage>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include <QWidget>

class UdpFramePipelineWorker;

class UdpFrameProcessor : public QWidget {
    Q_OBJECT

public:
    explicit UdpFrameProcessor(QWidget *parent = nullptr);
    ~UdpFrameProcessor();

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
    void saveSnapshotRequested(const QString &directory);
    void toggleRecordingRequested(const QString &directory, const QString &format, int fps);
    void flipHorizontalRequested(bool enabled);
    void flipVerticalRequested(bool enabled);
    void brightnessRequested(int value);
    void gammaRequested(int value);
    void sharpnessRequested(int value);
    void denoiseRequested(int value);
    void receiverSettingsApplyRequested(const QString &address, quint16 port);
    void aiDetectionRequested(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void presentLatestFrame();
    void onWorkerFrameReady(const QImage &frame);
    void onWorkerStatsReady(const QString &statsText);

private:
    QThread *workerThread;
    UdpFramePipelineWorker *worker;
    QTimer *presentTimer;

    QImage pendingFrame;
    QImage presentedFrame;
    QMutex frameMutex;
    bool framePendingPresentation;
    int presentedFrameCount;
    QString latestWorkerStats;
};

#endif // UDP_FRAME_PROCESSOR_H
