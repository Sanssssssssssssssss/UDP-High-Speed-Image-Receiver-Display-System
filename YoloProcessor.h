#ifndef YOLOPROCESSOR_H
#define YOLOPROCESSOR_H

#include <QObject>
#include <QThread>
#include <QImage>
#include <QByteArray>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <atomic>  
#include <QMutex>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <QBuffer>

class YoloProcessor : public QObject {
Q_OBJECT

public:
    explicit YoloProcessor(QObject *parent = nullptr);

    bool isProcessing() const; 

public slots:
    void runInference(); 
    void addPixel(int x, int y, uchar r, uchar g, uchar b);  
    void frameReady();  

signals:
    void detectionFinished(std::vector<QRect> boxes);  

private:
    cv::dnn::Net net;
    QImage frameBuffer;  
    QByteArray lastFrameJpg;  
    std::atomic<bool> processing{false};  
    QMutex bufferMutex;
};

#endif // YOLOPROCESSOR_H
