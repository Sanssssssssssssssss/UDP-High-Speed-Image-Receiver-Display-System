#ifndef YOLO_PROCESSOR_H
#define YOLO_PROCESSOR_H

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QRect>
#include <QVector>
#include <atomic>
#include <opencv2/dnn.hpp>

class YoloProcessor : public QObject {
    Q_OBJECT

public:
    explicit YoloProcessor(const QString &modelPath, QObject *parent = nullptr);

public slots:
    void setEnabled(bool enabled);
    void submitFrame(const QImage &frame);

signals:
    void detectionsReady(const QVector<QRect> &boxes, int inferenceMs);
    void statusChanged(const QString &statusText);

private slots:
    void processLatestFrame();

private:
    bool loadModel(const QString &modelPath);
    QVector<QRect> runInference(const QImage &frame, int &inferenceMs) const;

    cv::dnn::Net net;
    QString activeModelPath;
    QMutex frameMutex;
    QImage latestFrame;
    std::atomic<bool> enabled;
    std::atomic<bool> modelLoaded;
    std::atomic<bool> processing;
    bool frameQueued;
};

#endif // YOLO_PROCESSOR_H
