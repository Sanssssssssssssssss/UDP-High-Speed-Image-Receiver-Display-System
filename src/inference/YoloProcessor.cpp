#include "YoloProcessor.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace {
constexpr int kModelInputSize = 416;
constexpr float kConfidenceThreshold = 0.85f;
constexpr float kNmsScoreThreshold = 0.3f;
constexpr float kNmsThreshold = 0.5f;

QString firstExistingPath(const QStringList &candidates) {
    for (QStringList::const_iterator it = candidates.cbegin(); it != candidates.cend(); ++it) {
        const QString cleaned = QDir::cleanPath(*it);
        if (QFileInfo::exists(cleaned)) {
            return cleaned;
        }
    }
    return QString();
}

QStringList buildRepoRelativeCandidates(const QString &tailPath) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString currentDir = QDir::currentPath();
    QStringList candidates;
    candidates << QDir(appDir).filePath(tailPath)
               << QDir(appDir).filePath(QString("../%1").arg(tailPath))
               << QDir(appDir).filePath(QString("../../%1").arg(tailPath))
               << QDir(appDir).filePath(QString("../../../%1").arg(tailPath))
               << QDir(currentDir).filePath(tailPath);
    return candidates;
}
}

YoloProcessor::YoloProcessor(const QString &modelPathValue, QObject *parent)
    : QObject(parent),
      modelPath(modelPathValue),
      helperProcess(nullptr),
      enabled(false),
      processing(false),
      frameQueued(false),
      backendMode(BackendUnavailable) {
}

YoloProcessor::~YoloProcessor() {
    if (helperProcess != nullptr) {
        helperProcess->closeWriteChannel();
        helperProcess->kill();
        helperProcess->waitForFinished(1000);
        delete helperProcess;
        helperProcess = nullptr;
    }
}

void YoloProcessor::initialize() {
    QString statusText;
    if (tryLoadOpenCvBackend(statusText)) {
        emit statusChanged(statusText);
        return;
    }

    const QString openCvFailure = statusText;
    if (tryStartPythonHelper(statusText)) {
        emit statusChanged(QString("%1 | Fallback: %2").arg(openCvFailure, statusText));
        return;
    }

    backendStatus = QString("%1 | %2").arg(openCvFailure, statusText);
    emit statusChanged(backendStatus);
}

bool YoloProcessor::tryLoadOpenCvBackend(QString &statusText) {
    try {
        net = cv::dnn::readNetFromONNX(modelPath.toStdString());
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        backendMode = BackendOpenCv;
        backendStatus = QString("AI model loaded via OpenCV DNN: %1").arg(modelPath);
        statusText = backendStatus;
        return true;
    } catch (const cv::Exception &e) {
        backendMode = BackendUnavailable;
        statusText = QString("OpenCV DNN could not load ONNX: %1").arg(QString::fromStdString(e.what()));
        return false;
    }
}

bool YoloProcessor::tryStartPythonHelper(QString &statusText) {
    helperPythonPath = firstExistingPath(buildRepoRelativeCandidates(".local/py310-yolo/Scripts/python.exe"));
    helperScriptPath = firstExistingPath(buildRepoRelativeCandidates("scripts/onnx_helper.py"));

    if (helperPythonPath.isEmpty() || helperScriptPath.isEmpty() || !QFileInfo::exists(modelPath)) {
        statusText = QString("Python ONNX fallback unavailable. python=%1 script=%2 model=%3")
                         .arg(helperPythonPath.isEmpty() ? "missing" : helperPythonPath)
                         .arg(helperScriptPath.isEmpty() ? "missing" : helperScriptPath)
                         .arg(QFileInfo::exists(modelPath) ? modelPath : QString("missing: %1").arg(modelPath));
        return false;
    }

    helperProcess = new QProcess(this);
    helperProcess->setProcessChannelMode(QProcess::SeparateChannels);
    helperProcess->start(helperPythonPath, QStringList() << helperScriptPath << modelPath);
    if (!helperProcess->waitForStarted(5000)) {
        statusText = QString("Python ONNX fallback failed to start: %1").arg(helperProcess->errorString());
        delete helperProcess;
        helperProcess = nullptr;
        return false;
    }

    if (!helperProcess->waitForReadyRead(10000)) {
        const QString stderrText = QString::fromUtf8(helperProcess->readAllStandardError());
        statusText = QString("Python ONNX fallback did not become ready: %1").arg(stderrText.trimmed());
        helperProcess->kill();
        helperProcess->waitForFinished(1000);
        delete helperProcess;
        helperProcess = nullptr;
        return false;
    }

    const QByteArray line = helperProcess->readLine().trimmed();
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject() || !doc.object().value("ok").toBool()) {
        const QString stderrText = QString::fromUtf8(helperProcess->readAllStandardError());
        statusText = QString("Python ONNX fallback init failed: %1 %2")
                         .arg(QString::fromUtf8(line))
                         .arg(stderrText.trimmed());
        helperProcess->kill();
        helperProcess->waitForFinished(1000);
        delete helperProcess;
        helperProcess = nullptr;
        return false;
    }

    backendMode = BackendPythonHelper;
    backendStatus = QString("AI model loaded via Python ONNX helper: %1").arg(modelPath);
    statusText = backendStatus;
    return true;
}

void YoloProcessor::setEnabled(bool enabledValue) {
    enabled.store(enabledValue);
    if (!enabledValue) {
        emit detectionsReady(QVector<QRect>(), 0);
        emit statusChanged("AI detection is disabled.");
        return;
    }

    if (backendMode == BackendUnavailable) {
        emit statusChanged(QString("AI detection cannot start. %1").arg(backendStatus));
    } else {
        emit statusChanged(QString("AI detection is enabled. %1").arg(backendStatus));
    }
}

void YoloProcessor::submitFrame(const QImage &frame) {
    if (!enabled.load() || backendMode == BackendUnavailable || frame.isNull()) {
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
        QVector<QRect> boxes;
        if (backendMode == BackendOpenCv) {
            boxes = runOpenCvInference(frame, inferenceMs);
        } else if (backendMode == BackendPythonHelper) {
            boxes = runPythonInference(frame, inferenceMs);
        }
        emit detectionsReady(boxes, inferenceMs);
    }
}

QVector<QRect> YoloProcessor::runOpenCvInference(const QImage &frame, int &inferenceMs) const {
    QVector<QRect> detections;
    cv::Mat rgb(frame.height(), frame.width(), CV_8UC3,
                const_cast<uchar *>(frame.constBits()), frame.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

    cv::Mat blob;
    cv::dnn::blobFromImage(bgr, blob, 1.0 / 255.0, cv::Size(kModelInputSize, kModelInputSize), cv::Scalar(), true, false);

    QElapsedTimer timer;
    timer.start();
    cv::dnn::Net localNet = net;
    localNet.setInput(blob);
    std::vector<cv::Mat> outputs;
    localNet.forward(outputs);
    inferenceMs = static_cast<int>(timer.elapsed());

    if (outputs.empty()) {
        return detections;
    }

    cv::Mat output = outputs[0].reshape(1, 5).t();
    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    const double scaleX = static_cast<double>(frame.width()) / static_cast<double>(kModelInputSize);
    const double scaleY = static_cast<double>(frame.height()) / static_cast<double>(kModelInputSize);

    for (int i = 0; i < output.rows; ++i) {
        float *data = output.ptr<float>(i);
        const float cx = data[0];
        const float cy = data[1];
        const float w = data[2];
        const float h = data[3];
        const float score = data[4];

        if (score < kConfidenceThreshold) {
            continue;
        }

        const int x1 = std::max(0, std::min(frame.width(), static_cast<int>((cx - (w * 0.5f)) * scaleX)));
        const int y1 = std::max(0, std::min(frame.height(), static_cast<int>((cy - (h * 0.5f)) * scaleY)));
        const int x2 = std::max(0, std::min(frame.width(), static_cast<int>((cx + (w * 0.5f)) * scaleX)));
        const int y2 = std::max(0, std::min(frame.height(), static_cast<int>((cy + (h * 0.5f)) * scaleY)));

        boxes.push_back(cv::Rect(x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1)));
        scores.push_back(score);
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, scores, kNmsScoreThreshold, kNmsThreshold, indices);
    for (size_t i = 0; i < indices.size(); ++i) {
        const cv::Rect &box = boxes[static_cast<size_t>(indices[i])];
        detections.push_back(QRect(box.x, box.y, box.width, box.height));
    }

    return detections;
}

QVector<QRect> YoloProcessor::runPythonInference(const QImage &frame, int &inferenceMs) {
    QVector<QRect> detections;
    inferenceMs = 0;
    if (helperProcess == nullptr || helperProcess->state() != QProcess::Running) {
        return detections;
    }

    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    frame.save(&buffer, "PNG");

    QJsonObject request;
    request.insert("image", QString::fromLatin1(encoded.toBase64()));
    request.insert("width", frame.width());
    request.insert("height", frame.height());
    request.insert("input_size", kModelInputSize);
    request.insert("confidence", kConfidenceThreshold);
    request.insert("nms_score", kNmsScoreThreshold);
    request.insert("nms_threshold", kNmsThreshold);

    helperProcess->write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    helperProcess->write("\n");
    helperProcess->waitForBytesWritten(5000);

    if (!helperProcess->waitForReadyRead(10000)) {
        backendStatus = QString("Python ONNX helper timed out: %1").arg(QString::fromUtf8(helperProcess->readAllStandardError()).trimmed());
        emit statusChanged(backendStatus);
        return detections;
    }

    const QByteArray line = helperProcess->readLine().trimmed();
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) {
        backendStatus = QString("Python ONNX helper returned invalid JSON: %1").arg(QString::fromUtf8(line));
        emit statusChanged(backendStatus);
        return detections;
    }

    const QJsonObject obj = doc.object();
    if (!obj.value("ok").toBool()) {
        backendStatus = QString("Python ONNX helper inference failed: %1").arg(obj.value("error").toString());
        emit statusChanged(backendStatus);
        return detections;
    }

    inferenceMs = obj.value("inference_ms").toInt();
    const QJsonArray boxes = obj.value("boxes").toArray();
    detections.reserve(boxes.size());
    for (int i = 0; i < boxes.size(); ++i) {
        const QJsonArray arr = boxes[i].toArray();
        if (arr.size() != 4) {
            continue;
        }
        detections.push_back(QRect(arr[0].toInt(), arr[1].toInt(), arr[2].toInt(), arr[3].toInt()));
    }

    return detections;
}
