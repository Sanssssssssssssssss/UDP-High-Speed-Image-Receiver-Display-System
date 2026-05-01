#include "YoloProcessor.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <algorithm>
#include <cmath>
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

bool cppPreprocessEnabled() {
    const QByteArray value = qgetenv("POST_TRAIN_CPP_PREPROCESS").trimmed().toLower();
    return value.isEmpty() || value == "1" || value == "true" || value == "yes" || value == "on";
}

bool writeFully(QProcess *process, const char *data, qint64 size, int timeoutMs, QString &errorText) {
    qint64 offset = 0;
    while (offset < size) {
        if (process->state() != QProcess::Running) {
            errorText = QString("helper process is not running: %1").arg(process->errorString());
            return false;
        }

        const qint64 written = process->write(data + offset, size - offset);
        if (written < 0) {
            errorText = QString("helper write failed: %1").arg(process->errorString());
            return false;
        }

        if (written == 0) {
            if (!process->waitForBytesWritten(timeoutMs)) {
                errorText = QString("helper write timed out: %1").arg(process->errorString());
                return false;
            }
            continue;
        }

        offset += written;
    }

    if (!process->waitForBytesWritten(timeoutMs)) {
        errorText = QString("helper write flush timed out: %1").arg(process->errorString());
        return false;
    }

    return true;
}

QByteArray readLineWithTimeout(QProcess *process, int timeoutMs, QString &errorText) {
    QByteArray line;
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (process->canReadLine()) {
            line += process->readLine();
            break;
        }

        const QByteArray partial = process->readLine();
        if (!partial.isEmpty()) {
            line += partial;
            if (line.endsWith('\n')) {
                break;
            }
            continue;
        }

        if (process->state() != QProcess::Running) {
            errorText = QString("helper process exited: %1").arg(QString::fromUtf8(process->readAllStandardError()).trimmed());
            return line;
        }

        const int remainingMs = static_cast<int>(std::max<qint64>(1, timeoutMs - timer.elapsed()));
        if (!process->waitForReadyRead(remainingMs)) {
            break;
        }
    }

    if (line.isEmpty()) {
        errorText = QString("helper read timed out: %1").arg(QString::fromUtf8(process->readAllStandardError()).trimmed());
    }

    return line.trimmed();
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
        backendMode.store(BackendOpenCv);
        backendStatus = QString("AI model loaded via OpenCV DNN: %1").arg(modelPath);
        statusText = backendStatus;
        return true;
    } catch (const cv::Exception &e) {
        backendMode.store(BackendUnavailable);
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
    helperProcess->setWorkingDirectory(QFileInfo(helperScriptPath).absolutePath());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("PYTHONHOME");
    env.remove("PYTHONPATH");
    env.remove("PYTHONEXECUTABLE");
    env.remove("PYTHONSTARTUP");
    env.remove("PYTHONUSERBASE");
    env.remove("PYTHONBREAKPOINT");
    env.insert("PYTHONNOUSERSITE", "1");

    const QString helperScriptsDir = QFileInfo(helperPythonPath).absolutePath();
    const QString helperVenvRoot = QDir(helperScriptsDir).filePath("..");
    env.insert("VIRTUAL_ENV", QDir::cleanPath(helperVenvRoot));
    env.insert("PATH", helperScriptsDir + ";" + env.value("PATH"));
    helperProcess->setProcessEnvironment(env);

    helperProcess->start(helperPythonPath, QStringList() << "-I" << helperScriptPath << modelPath);
    if (!helperProcess->waitForStarted(5000)) {
        statusText = QString("Python ONNX fallback failed to start: %1").arg(helperProcess->errorString());
        delete helperProcess;
        helperProcess = nullptr;
        return false;
    }

    QString readError;
    const QByteArray line = readLineWithTimeout(helperProcess, 20000, readError);
    if (line.isEmpty()) {
        statusText = QString("Python ONNX fallback did not become ready: %1").arg(readError);
        helperProcess->kill();
        helperProcess->waitForFinished(1000);
        delete helperProcess;
        helperProcess = nullptr;
        return false;
    }

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

    const QString provider = doc.object().value("provider").toString();
    const QString protocol = doc.object().value("request_protocol").toString();
    const int intraThreads = doc.object().value("intra_threads").toInt();
    const int warmupMs = doc.object().value("warmup_ms").toInt();
    backendMode.store(BackendPythonHelper);
    backendStatus = provider.isEmpty()
        ? QString("AI model loaded via Python ONNX helper: %1").arg(modelPath)
        : QString("AI model loaded via Python ONNX helper (%1, protocol=%2, intra=%3, warmup=%4 ms): %5")
              .arg(provider)
              .arg(protocol.isEmpty() ? "json" : protocol)
              .arg(intraThreads)
              .arg(warmupMs)
              .arg(modelPath);
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

    if (backendMode.load() == BackendUnavailable) {
        emit statusChanged(QString("AI detection cannot start. %1").arg(backendStatus));
    } else {
        emit statusChanged(QString("AI detection is enabled. %1").arg(backendStatus));
    }
}

bool YoloProcessor::wantsFrame() {
    if (!enabled.load() || backendMode.load() == BackendUnavailable) {
        return false;
    }

    QMutexLocker lock(&frameMutex);
    return !frameQueued;
}

void YoloProcessor::submitFrame(const QImage &frame) {
    if (!enabled.load() || backendMode.load() == BackendUnavailable || frame.isNull()) {
        return;
    }

    {
        QMutexLocker lock(&frameMutex);
        latestFrame = frame;
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
        const int mode = backendMode.load();
        if (mode == BackendOpenCv) {
            boxes = runOpenCvInference(frame, inferenceMs);
        } else if (mode == BackendPythonHelper) {
            boxes = runPythonInference(frame, inferenceMs);
        }
        emit detectionsReady(boxes, inferenceMs);
    }
}

QVector<QRect> YoloProcessor::runOpenCvInference(const QImage &frame, int &inferenceMs) {
    QVector<QRect> detections;
    cv::Mat rgb(frame.height(), frame.width(), CV_8UC3,
                const_cast<uchar *>(frame.constBits()), frame.bytesPerLine());

    cv::dnn::blobFromImage(rgb, openCvBlob, 1.0 / 255.0, cv::Size(kModelInputSize, kModelInputSize), cv::Scalar(), false, false);

    QElapsedTimer timer;
    timer.start();
    net.setInput(openCvBlob);
    openCvOutputs.clear();
    net.forward(openCvOutputs);
    inferenceMs = static_cast<int>(timer.elapsed());

    if (openCvOutputs.empty()) {
        return detections;
    }

    cv::Mat output = openCvOutputs[0].reshape(1, 5).t();
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

    const QImage rgbFrame = frame.format() == QImage::Format_RGB888
        ? frame
        : frame.convertToFormat(QImage::Format_RGB888);
    const int sourceWidth = rgbFrame.width();
    const int sourceHeight = rgbFrame.height();
    const uchar *payloadBits = rgbFrame.constBits();
    int payloadWidth = sourceWidth;
    int payloadHeight = sourceHeight;
    int payloadStride = rgbFrame.bytesPerLine();
    QString protocol = "rgb24-binary-v1";

    if (cppPreprocessEnabled()) {
        if (sourceWidth != kModelInputSize || sourceHeight != kModelInputSize) {
            cv::Mat sourceRgb(sourceHeight, sourceWidth, CV_8UC3,
                              const_cast<uchar *>(rgbFrame.constBits()),
                              rgbFrame.bytesPerLine());
            helperInputRgb.create(kModelInputSize, kModelInputSize, CV_8UC3);
            cv::resize(sourceRgb, helperInputRgb, cv::Size(kModelInputSize, kModelInputSize), 0.0, 0.0, cv::INTER_LINEAR);
            payloadBits = helperInputRgb.ptr<uchar>(0);
            payloadWidth = helperInputRgb.cols;
            payloadHeight = helperInputRgb.rows;
            payloadStride = static_cast<int>(helperInputRgb.step);
        }
        protocol = "rgb24-resized-binary-v1";
    }

    const qint64 rawByteCount = static_cast<qint64>(payloadStride) * static_cast<qint64>(payloadHeight);

    QJsonObject request;
    request.insert("protocol", protocol);
    request.insert("image_rgb24_bytes", static_cast<int>(rawByteCount));
    request.insert("width", payloadWidth);
    request.insert("height", payloadHeight);
    request.insert("stride", payloadStride);
    request.insert("frame_width", sourceWidth);
    request.insert("frame_height", sourceHeight);
    request.insert("input_size", kModelInputSize);
    request.insert("confidence", kConfidenceThreshold);
    request.insert("nms_score", kNmsScoreThreshold);
    request.insert("nms_threshold", kNmsThreshold);

    QByteArray header = QJsonDocument(request).toJson(QJsonDocument::Compact);
    header.append('\n');

    QString ioError;
    if (!writeFully(helperProcess, header.constData(), header.size(), 5000, ioError)
        || !writeFully(helperProcess, reinterpret_cast<const char *>(payloadBits), rawByteCount, 5000, ioError)) {
        backendStatus = QString("Python ONNX helper request failed: %1").arg(ioError);
        emit statusChanged(backendStatus);
        return detections;
    }

    const QByteArray line = readLineWithTimeout(helperProcess, 10000, ioError);
    if (line.isEmpty()) {
        backendStatus = QString("Python ONNX helper timed out: %1").arg(ioError);
        emit statusChanged(backendStatus);
        return detections;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (!doc.isObject()) {
        backendStatus = QString("Python ONNX helper returned invalid JSON: %1 (%2)")
                            .arg(QString::fromUtf8(line))
                            .arg(parseError.errorString());
        emit statusChanged(backendStatus);
        return detections;
    }

    const QJsonObject obj = doc.object();
    if (!obj.value("ok").toBool()) {
        backendStatus = QString("Python ONNX helper inference failed: %1").arg(obj.value("error").toString());
        emit statusChanged(backendStatus);
        return detections;
    }

    inferenceMs = static_cast<int>(std::round(obj.value("total_ms").toDouble(obj.value("inference_ms").toDouble())));
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
