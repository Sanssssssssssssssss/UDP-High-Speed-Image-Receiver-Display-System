#ifndef YOLO_PROCESSOR_H
#define YOLO_PROCESSOR_H

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QRect>
#include <QVector>
#include <atomic>
#include <opencv2/dnn.hpp>

class YoloProcessor : public QObject {
    Q_OBJECT

public:
    explicit YoloProcessor(const QString &modelPath, QObject *parent = nullptr);
    ~YoloProcessor();
    bool wantsFrame();

public slots:
    void initialize();
    void setEnabled(bool enabled);
    void submitFrame(const QImage &frame);

signals:
    void detectionsReady(const QVector<QRect> &boxes, int inferenceMs);
    void statusChanged(const QString &statusText);

private slots:
    void processLatestFrame();

private:
    enum BackendMode {
        BackendUnavailable,
        BackendOpenCv,
        BackendPythonHelper
    };

    bool tryLoadOpenCvBackend(QString &statusText);
    bool tryStartPythonHelper(QString &statusText);
    QVector<QRect> runOpenCvInference(const QImage &frame, int &inferenceMs) const;
    QVector<QRect> runPythonInference(const QImage &frame, int &inferenceMs);

    QString modelPath;
    QString helperPythonPath;
    QString helperScriptPath;
    cv::dnn::Net net;
    QProcess *helperProcess;
    QMutex frameMutex;
    QImage latestFrame;
    std::atomic<bool> enabled;
    std::atomic<bool> processing;
    bool frameQueued;
    std::atomic<int> backendMode;
    QString backendStatus;
};

#endif // YOLO_PROCESSOR_H
