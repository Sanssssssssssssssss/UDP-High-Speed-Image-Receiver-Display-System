#include "YoloProcessor.h"
#include <QElapsedTimer>
#include <QMetaObject>
#include <QMutexLocker>
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace {
constexpr int kModelInputSize = 640;
constexpr float kConfidenceThreshold = 0.35f;
constexpr float kNmsThreshold = 0.45f;
}

YoloProcessor::YoloProcessor(const QString &modelPath, QObject *parent)
    : QObject(parent),
      enabled(false),
      modelLoaded(false),
      processing(false),
      frameQueued(false) {
    const bool loaded = loadModel(modelPath);
    modelLoaded.store(loaded);
    if (loaded) {
        emit statusChanged(QString("AI model loaded: %1").arg(activeModelPath));
    } else {
        emit statusChanged(QString("AI model load failed: %1").arg(modelPath));
    }
}

bool YoloProcessor::loadModel(const QString &modelPath) {
    try {
        net = cv::dnn::readNetFromONNX(modelPath.toStdString());
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        activeModelPath = modelPath;
        return true;
    } catch (const cv::Exception &) {
        activeModelPath.clear();
        return false;
    }
}

void YoloProcessor::setEnabled(bool enabledValue) {
    enabled.store(enabledValue);
    if (!enabledValue) {
        emit detectionsReady(QVector<QRect>(), 0);
        emit statusChanged(modelLoaded.load()
                               ? "AI detection is disabled."
                               : "AI detection is disabled. Model is not loaded.");
        return;
    }

    emit statusChanged(modelLoaded.load()
                           ? QString("AI detection is enabled. Model: %1").arg(activeModelPath)
                           : "AI detection is enabled, but the ONNX model is not loaded.");
}

void YoloProcessor::submitFrame(const QImage &frame) {
    if (!enabled.load() || !modelLoaded.load() || frame.isNull()) {
        return;
    }

    {
        QMutexLocker lock(&frameMutex);
        latestFrame = frame.copy();
        frameQueued = true;
    }

    if (!processing.exchange(true)) {
        QMetaObject::invokeMethod(this, "processLatestFrame", Qt::QueuedConnection);
    }
}

void YoloProcessor::processLatestFrame() {
    while (true) {
        QImage frame;
        {
            QMutexLocker lock(&frameMutex);
            if (!frameQueued) {
                processing.store(false);
                return;
            }
            frame = latestFrame;
            frameQueued = false;
        }

        int inferenceMs = 0;
        const QVector<QRect> boxes = runInference(frame, inferenceMs);
        emit detectionsReady(boxes, inferenceMs);
    }
}

QVector<QRect> YoloProcessor::runInference(const QImage &frame, int &inferenceMs) const {
    QVector<QRect> detections;
    if (frame.isNull()) {
        inferenceMs = 0;
        return detections;
    }

    cv::Mat rgb(frame.height(), frame.width(), CV_8UC3,
                const_cast<uchar *>(frame.constBits()), frame.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

    cv::Mat blob;
    cv::dnn::blobFromImage(bgr,
                           blob,
                           1.0 / 255.0,
                           cv::Size(kModelInputSize, kModelInputSize),
                           cv::Scalar(),
                           true,
                           false);

    QElapsedTimer timer;
    timer.start();

    cv::dnn::Net localNet = net;
    localNet.setInput(blob);
    std::vector<cv::Mat> outputs;
    localNet.forward(outputs, localNet.getUnconnectedOutLayersNames());
    inferenceMs = static_cast<int>(timer.elapsed());

    if (outputs.empty()) {
        return detections;
    }

    cv::Mat output = outputs[0];
    cv::Mat parsed;

    if (output.dims == 3) {
        const int dim1 = output.size[1];
        const int dim2 = output.size[2];
        cv::Mat raw(dim1, dim2, CV_32F, output.ptr<float>());
        if (dim1 < dim2) {
            cv::transpose(raw, parsed);
        } else {
            parsed = raw;
        }
    } else if (output.dims == 2) {
        parsed = output;
    } else {
        return detections;
    }

    if (parsed.empty() || parsed.cols < 5) {
        return detections;
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    const float scaleX = static_cast<float>(frame.width()) / static_cast<float>(kModelInputSize);
    const float scaleY = static_cast<float>(frame.height()) / static_cast<float>(kModelInputSize);

    for (int row = 0; row < parsed.rows; ++row) {
        const float *data = parsed.ptr<float>(row);
        float confidence = 0.0f;
        if (parsed.cols == 5) {
            confidence = data[4];
        } else {
            confidence = *std::max_element(data + 4, data + parsed.cols);
        }

        if (confidence < kConfidenceThreshold) {
            continue;
        }

        const float cx = data[0] * scaleX;
        const float cy = data[1] * scaleY;
        const float w = data[2] * scaleX;
        const float h = data[3] * scaleY;

        const int x = std::max(0, static_cast<int>(cx - (w * 0.5f)));
        const int y = std::max(0, static_cast<int>(cy - (h * 0.5f)));
        const int width = std::min(frame.width() - x, static_cast<int>(w));
        const int height = std::min(frame.height() - y, static_cast<int>(h));
        if (width <= 0 || height <= 0) {
            continue;
        }

        boxes.push_back(cv::Rect(x, y, width, height));
        scores.push_back(confidence);
    }

    std::vector<int> kept;
    cv::dnn::NMSBoxes(boxes, scores, kConfidenceThreshold, kNmsThreshold, kept);
    detections.reserve(static_cast<int>(kept.size()));
    for (size_t i = 0; i < kept.size(); ++i) {
        const cv::Rect &box = boxes[static_cast<size_t>(kept[i])];
        detections.push_back(QRect(box.x, box.y, box.width, box.height));
    }

    return detections;
}
