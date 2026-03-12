#ifndef VIDEO_RECORDER_WORKER_H
#define VIDEO_RECORDER_WORKER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QQueue>
#include <QString>
#include <opencv2/opencv.hpp>

class VideoRecorderWorker : public QObject {
    Q_OBJECT

public:
    explicit VideoRecorderWorker(QObject *parent = nullptr);
    ~VideoRecorderWorker();

public slots:
    void startRecording(const QString &directory, const QString &format, int fps, const QSize &frameSize);
    void stopRecording();
    void enqueueFrame(const QImage &frame);

signals:
    void recordingStateChanged(bool recording);

private slots:
    void processQueue();

private:
    void clearQueue();

    cv::VideoWriter videoWriter;
    QQueue<QImage> pendingFrames;
    QMutex queueMutex;
    bool recording;
    bool drainScheduled;
};

#endif // VIDEO_RECORDER_WORKER_H
