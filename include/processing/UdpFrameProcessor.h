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

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void presentLatestFrame();
    void onWorkerFrameReady(const QImage &frame);
    void onWorkerStatsReady(const QString &statsText);

private:
    void invokeWorkerVoid(const char *method);
    template <typename Arg1>
    void invokeWorkerUnary(const char *method, const Arg1 &arg1);
    template <typename Arg1, typename Arg2>
    void invokeWorkerBinary(const char *method, const Arg1 &arg1, const Arg2 &arg2);
    template <typename Arg1, typename Arg2, typename Arg3>
    void invokeWorkerTernary(const char *method, const Arg1 &arg1, const Arg2 &arg2, const Arg3 &arg3);

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

template <typename Arg1>
void UdpFrameProcessor::invokeWorkerUnary(const char *method, const Arg1 &arg1) {
    QMetaObject::invokeMethod(worker, method, Qt::QueuedConnection, Q_ARG(Arg1, arg1));
}

template <typename Arg1, typename Arg2>
void UdpFrameProcessor::invokeWorkerBinary(const char *method, const Arg1 &arg1, const Arg2 &arg2) {
    QMetaObject::invokeMethod(worker, method, Qt::QueuedConnection, Q_ARG(Arg1, arg1), Q_ARG(Arg2, arg2));
}

template <typename Arg1, typename Arg2, typename Arg3>
void UdpFrameProcessor::invokeWorkerTernary(const char *method, const Arg1 &arg1, const Arg2 &arg2, const Arg3 &arg3) {
    QMetaObject::invokeMethod(worker,
                              method,
                              Qt::QueuedConnection,
                              Q_ARG(Arg1, arg1),
                              Q_ARG(Arg2, arg2),
                              Q_ARG(Arg3, arg3));
}

#endif // UDP_FRAME_PROCESSOR_H
