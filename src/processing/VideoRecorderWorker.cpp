#include "VideoRecorderWorker.h"
#include <QDateTime>
#include <QDir>
#include <QMetaObject>
#include <opencv2/imgproc.hpp>

namespace {
const int kMaxQueuedRecordFrames = 6;
}

VideoRecorderWorker::VideoRecorderWorker(QObject *parent)
    : QObject(parent),
      recording(false),
      drainScheduled(false) {
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<QSize>("QSize");
}

VideoRecorderWorker::~VideoRecorderWorker() {
    stopRecording();
}

void VideoRecorderWorker::startRecording(const QString &directory, const QString &format, int fps, const QSize &frameSize) {
    stopRecording();

    QDir dir(directory);
    if (!dir.exists() && !dir.mkpath(".")) {
        emit recordingStateChanged(false);
        return;
    }

    const QString extension = format.toLower();
    const QString fileName = directory + "/recording_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "." + extension;
    const int codec = (extension == "avi")
        ? cv::VideoWriter::fourcc('M', 'J', 'P', 'G')
        : cv::VideoWriter::fourcc('m', 'p', '4', 'v');

    try {
        videoWriter.open(fileName.toStdString(), codec, fps, cv::Size(frameSize.width(), frameSize.height()));
    } catch (const cv::Exception &) {
        emit recordingStateChanged(false);
        return;
    }

    if (!videoWriter.isOpened()) {
        emit recordingStateChanged(false);
        return;
    }

    recording = true;
    emit recordingStateChanged(true);
}

void VideoRecorderWorker::stopRecording() {
    clearQueue();

    if (videoWriter.isOpened()) {
        videoWriter.release();
    }

    if (recording) {
        recording = false;
        emit recordingStateChanged(false);
    }
}

void VideoRecorderWorker::enqueueFrame(const QImage &frame) {
    if (!recording || !videoWriter.isOpened() || frame.isNull()) {
        return;
    }

    bool shouldScheduleDrain = false;
    {
        QMutexLocker lock(&queueMutex);
        if (pendingFrames.size() >= kMaxQueuedRecordFrames) {
            pendingFrames.dequeue();
        }
        pendingFrames.enqueue(frame);
        if (!drainScheduled) {
            drainScheduled = true;
            shouldScheduleDrain = true;
        }
    }

    if (shouldScheduleDrain) {
        QMetaObject::invokeMethod(this, "processQueue", Qt::QueuedConnection);
    }
}

void VideoRecorderWorker::processQueue() {
    while (true) {
        QImage frame;
        {
            QMutexLocker lock(&queueMutex);
            if (pendingFrames.isEmpty()) {
                drainScheduled = false;
                break;
            }
            frame = pendingFrames.dequeue();
        }

        cv::Mat rgb(frame.height(), frame.width(), CV_8UC3, const_cast<uchar *>(frame.bits()), frame.bytesPerLine());
        cv::Mat bgr;
        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
        if (!bgr.empty()) {
            videoWriter.write(bgr);
        }
    }
}

void VideoRecorderWorker::clearQueue() {
    QMutexLocker lock(&queueMutex);
    pendingFrames.clear();
    drainScheduled = false;
}
