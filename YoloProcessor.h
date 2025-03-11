#ifndef YOLOPROCESSOR_H
#define YOLOPROCESSOR_H

#include <QObject>
#include <QThread>
#include <QImage>
#include <QByteArray>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <atomic>  // ✅ **确保 `atomic` 变量不会死锁**
#include <QMutex>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <QBuffer>

class YoloProcessor : public QObject {
Q_OBJECT

public:
    explicit YoloProcessor(QObject *parent = nullptr);

    bool isProcessing() const;  // ✅ **检查 YOLO 是否在推理**

public slots:
    void runInference();  // ✅ **异步执行 YOLO**
    void addPixel(int x, int y, uchar r, uchar g, uchar b);  // ✅ **逐像素存入 frameBuffer**
    void frameReady();  // ✅ **YOLO 线程启动推理**

signals:
    void detectionFinished(std::vector<QRect> boxes);  // ✅ **检测完成信号**

private:
    cv::dnn::Net net;
    QImage frameBuffer;  // ✅ **YOLO 线程维护自己的 frameBuffer**
    QByteArray lastFrameJpg;  // ✅ **存储最新转换的 JPG**
    std::atomic<bool> processing{false};  // ✅ **防止 YOLO 重复执行**
    QMutex bufferMutex;
};

#endif // YOLOPROCESSOR_H
