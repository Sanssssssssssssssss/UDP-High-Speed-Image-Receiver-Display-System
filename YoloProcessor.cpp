#include "YoloProcessor.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <QBuffer>
#include <QImageReader>
#include <QDir>

YoloProcessor::YoloProcessor(QObject *parent) : QObject(parent) {
    net = cv::dnn::readNetFromONNX("D:/yolov8n_416.onnx");
    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    frameBuffer = QImage(400, 400, QImage::Format_RGB888);
    frameBuffer.fill(Qt::black);
}

void YoloProcessor::addPixel(int x, int y, uchar r, uchar g, uchar b) {
    if (x >= 0 && x < frameBuffer.width() && y >= 0 && y < frameBuffer.height()) {
        frameBuffer.setPixel(x, y, qRgb(r, g, b));
    }
}

void YoloProcessor::frameReady() {
    // qDebug() << "[YOLO] frameReady() called!";

    if (processing.exchange(true)) {
        qDebug() << "[YOLO] Already processing, skipping...";
        return;
    }

    QtConcurrent::run([this]() {
        this->runInference();
    });
}

// yolo inference
void YoloProcessor::runInference() {
//    qDebug() << "[YOLO] runInference() STARTED!";
//    qDebug() << "[YOLO] Running inference on thread:" << QThread::currentThread();

    QImage localFrame;
    {
        QMutexLocker lock(&bufferMutex);
        localFrame = frameBuffer.copy();
    }

    if (localFrame.isNull()) {
//        qDebug() << "[YOLO] ERROR: FrameBuffer is null!";
        processing = false;
        return;
    }

    // **转换 QImage -> OpenCV Mat**
    cv::Mat mat(localFrame.height(), localFrame.width(), CV_8UC3,
                const_cast<uchar*>(localFrame.bits()), localFrame.bytesPerLine());

    //  ** BGR -> RGB**
    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);

    cv::Mat blob;
    cv::Size inputSize(416, 416);
    cv::dnn::blobFromImage(mat, blob, 1 / 255.0, inputSize, cv::Scalar(), true, false);

    // qDebug() << "[YOLO] OpenCV blob size:" << blob.size;

    net.setInput(blob);
    std::vector<cv::Mat> outputs;
    net.forward(outputs);

    cv::Mat output = outputs[0].reshape(1, 5).t();  // (3549, 5)

    // qDebug() << "[YOLO] Inference completed. Parsing results...";

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;

    for (int i = 0; i < output.rows; i++) {
        float* data = output.ptr<float>(i);  // 直接取出每一行数据
        float cx = data[0];
        float cy = data[1];
        float w = data[2];
        float h = data[3];
        float score = data[4];

        if (score < 0.85) continue;

        int x1 = std::max(0, std::min(mat.cols, static_cast<int>(cx - w / 2)));
        int y1 = std::max(0, std::min(mat.rows, static_cast<int>(cy - h / 2)));
        int x2 = std::max(0, std::min(mat.cols, static_cast<int>(cx + w / 2)));
        int y2 = std::max(0, std::min(mat.rows, static_cast<int>(cy + h / 2)));

        boxes.push_back(cv::Rect(x1, y1, x2 - x1, y2 - y1));
        scores.push_back(score);
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, scores, 0.3, 0.5, indices);

    std::vector<QRect> detectedBoxes;
//    qDebug() << "[YOLO] Raw Boxes Before NMS:";
//    for (size_t i = 0; i < boxes.size(); i++) {
//        qDebug() << "Box:" << boxes[i].x << boxes[i].y << boxes[i].width << boxes[i].height
//                 << "Score:" << scores[i];
//    }

    for (int i : indices) {
        int original_x = boxes[i].x;
        int original_y = boxes[i].y;
        int original_width = boxes[i].width;
        int original_height = boxes[i].height;

        int new_x = original_x * 2;
        int new_y = original_y * 2;
        int new_width = original_width * 2;
        int new_height = original_height * 2;

        detectedBoxes.push_back(QRect(new_x, new_y, new_width, new_height));
    }


    qDebug() << "[YOLO] Detected" << detectedBoxes.size() << "objects";

    emit detectionFinished(detectedBoxes);

    processing = false;
    // qDebug() << "[YOLO] Processing flag reset. Ready for next frame.";
}


bool YoloProcessor::isProcessing() const {
    bool status = processing.load();
    // qDebug() << "[YOLO] isProcessing() called. Current status:" << status;
    return status;
}

