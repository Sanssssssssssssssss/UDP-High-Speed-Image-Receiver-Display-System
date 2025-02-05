#ifndef UDP_FRAME_PROCESSOR_H
#define UDP_FRAME_PROCESSOR_H

#include <QDateTime>
#include <QWidget>
#include <QImage>
#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QMutex>
#include <QThread>
#include <QElapsedTimer>
#include <QDir>
#include <QDebug>
#include <QMutexLocker>
#include <opencv2/opencv.hpp>
#include "UdpReceiver.h"

class UdpFrameProcessor : public QWidget {
    Q_OBJECT

public:
    explicit UdpFrameProcessor(QWidget *parent = nullptr);
    ~UdpFrameProcessor();

    // Get the current frame image (thread-safe)
    QImage getCurrentFrame();

public slots:
    // Save a snapshot
    void saveSnapshot(const QString &directory);

    // Start/Stop video recording
    void toggleRecording(const QString &directory, const QString &format, int fps = 30);

    // Set horizontal image flip
    void setFlipHorizontal(bool enabled);

    // Set vertical image flip
    void setFlipVertical(bool enabled);

signals:
    // Signal emitted when recording state changes
    void recordingStateChanged(bool isRecording);

protected:
    // Handles the paint event to display the frame
    void paintEvent(QPaintEvent *event) override;

signals:
    // Updates FPS once per second
    void fpsChanged(int fps);

private slots:
    // Update FPS counter
    void updateFPS();

    // Process received frame data
    void processFrameData(const QByteArray &data);

private:
    // Helper function: Write the current frame to the video file
    void writeFrameToVideo();

    // Image data and thread synchronization
    QImage image;
    QMutex imageMutex;

    // FPS and recording timers
    QTimer *fpsTimer;
    QElapsedTimer recordingTimer;

    // Frame counter and received line count
    int frameCount;
    int receivedLines;

    // UDP receiver and processing thread
    UdpReceiver *receiver;
    QThread *receiverThread;

    // Image flipping states
    bool flipHorizontal;
    bool flipVertical;

    // OpenCV video writer for recording
    cv::VideoWriter videoWriter;

    // Recording state flag
    bool isRecording;
};

#endif // UDP_FRAME_PROCESSOR_H
