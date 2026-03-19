#include "UdpFramePipelineWorker.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <emmintrin.h>
#include <opencv2/imgproc.hpp>

constexpr int UdpFramePipelineWorker::kFrameWidth;
constexpr int UdpFramePipelineWorker::kFrameHeight;
constexpr int UdpFramePipelineWorker::kPacketHeaderSize;
constexpr int UdpFramePipelineWorker::kLinePayloadBytes;
constexpr int UdpFramePipelineWorker::kPacketsPerFrame;
constexpr int UdpFramePipelineWorker::kMaxQueuedFrames;
constexpr int UdpFramePipelineWorker::kMaxQueuedPackets;

namespace {
template <typename T>
T clampValue(T value, T low, T high) {
    return std::min(high, std::max(low, value));
}

const std::array<uchar, 32> kExpand5To8 = []() {
    std::array<uchar, 32> table = {};
    for (int i = 0; i < 32; ++i) {
        table[static_cast<size_t>(i)] = static_cast<uchar>((i << 3) | (i >> 2));
    }
    return table;
}();

const std::array<uchar, 64> kExpand6To8 = []() {
    std::array<uchar, 64> table = {};
    for (int i = 0; i < 64; ++i) {
        table[static_cast<size_t>(i)] = static_cast<uchar>((i << 2) | (i >> 4));
    }
    return table;
}();

QString resolvePackagedOnnxPath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath("models/best.onnx");
}
}

UdpFramePipelineWorker::UdpFramePipelineWorker(QObject *parent)
    : QObject(parent),
      receiver(nullptr),
      receiverThread(nullptr),
      yoloProcessor(nullptr),
      yoloThread(nullptr),
      recorderWorker(nullptr),
      recorderThread(nullptr),
      statsTimer(nullptr),
      flipHorizontal(false),
      flipVertical(false),
      brightnessValue(50),
      gammaValue(0),
      sharpnessValue(0),
      denoiseValue(0),
      lutDirty(true),
      receiverAddress("0.0.0.0"),
      receiverPort(8080),
      aiDetectionEnabled(false),
      aiStatusText("AI detection is disabled."),
      lastInferenceMs(0),
      isRecording(false),
      currentLine(0),
      frameValid(false),
      frameData(kFrameHeight * kLinePayloadBytes, 0),
      linePayloadSizes(kFrameHeight, 0),
      receivedLineFlags(kFrameHeight, 0),
      pendingPacketCount(0),
      drainScheduled(false),
      parserResyncPending(false),
      rawImage(kFrameWidth, kFrameHeight, QImage::Format_RGB888),
      frontDisplayIndex(0),
      datagramsThisSecond(0),
      completedFramesThisSecond(0),
      recoveredLinesThisSecond(0),
      frameProcessingNsThisSecond(0),
      interpolationNsThisSecond(0),
      droppedPacketsThisSecond(0),
      droppedBatchesThisSecond(0),
      maxQueuedPacketsThisSecond(0),
      drainNsThisSecond(0),
      maxDrainNsThisSecond(0),
      startMarkersThisSecond(0),
      endMarkersThisSecond(0),
      startWithoutEndThisSecond(0),
      endWithoutStartThisSecond(0),
      orphanLinePacketsThisSecond(0),
      overflowLinePacketsThisSecond(0),
      shortFrameEndsThisSecond(0),
      parserResyncEventsThisSecond(0) {
    rawImage.fill(Qt::black);
    displayBuffers[0] = rawImage.copy();
    displayBuffers[1] = rawImage.copy();
}

UdpFramePipelineWorker::~UdpFramePipelineWorker() {
    shutdown();
}

void UdpFramePipelineWorker::start() {
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<QSize>("QSize");
    qRegisterMetaType<QList<QByteArray> >("QList<QByteArray>");
    qRegisterMetaType<quint16>("quint16");
    qRegisterMetaType<QVector<QRect> >("QVector<QRect>");

    const QString packagedModelPath = resolvePackagedOnnxPath();
    aiStatusText = QFileInfo::exists(packagedModelPath)
        ? QString("AI model ready: %1").arg(packagedModelPath)
        : QString("AI model file missing: %1").arg(packagedModelPath);

    statsTimer = new QTimer(this);
    connect(statsTimer, &QTimer::timeout, this, &UdpFramePipelineWorker::updateStats);
    statsTimer->start(1000);

    yoloProcessor = new YoloProcessor(packagedModelPath);
    yoloThread = new QThread();
    yoloProcessor->moveToThread(yoloThread);
    connect(yoloThread, &QThread::finished, yoloProcessor, &QObject::deleteLater);
    connect(yoloProcessor, &YoloProcessor::detectionsReady, this, &UdpFramePipelineWorker::onYoloDetectionsReady, Qt::QueuedConnection);
    connect(yoloProcessor, &YoloProcessor::statusChanged, this, &UdpFramePipelineWorker::onYoloStatusChanged, Qt::QueuedConnection);
    yoloThread->start();
    QMetaObject::invokeMethod(yoloProcessor, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, false));

    recorderWorker = new VideoRecorderWorker();
    recorderThread = new QThread();
    recorderWorker->moveToThread(recorderThread);
    connect(recorderThread, &QThread::finished, recorderWorker, &QObject::deleteLater);
    connect(this, &UdpFramePipelineWorker::startRecordingRequested, recorderWorker, &VideoRecorderWorker::startRecording, Qt::QueuedConnection);
    connect(this, &UdpFramePipelineWorker::stopRecordingRequested, recorderWorker, &VideoRecorderWorker::stopRecording, Qt::QueuedConnection);
    connect(this, &UdpFramePipelineWorker::recordFrameReady, recorderWorker, &VideoRecorderWorker::enqueueFrame, Qt::QueuedConnection);
    connect(recorderWorker, &VideoRecorderWorker::recordingStateChanged, this, [this](bool recording) {
        isRecording = recording;
        emit recordingStateChanged(recording);
    }, Qt::QueuedConnection);
    recorderThread->start();

    receiver = new UdpReceiver();
    receiverThread = new QThread();
    receiver->moveToThread(receiverThread);
    connect(receiverThread, &QThread::started, receiver, [this]() { receiver->startReceiving(receiverAddress, receiverPort); });
    connect(receiver, &UdpReceiver::newFrameBatch, this, &UdpFramePipelineWorker::enqueueFrameBatch, Qt::DirectConnection);
    connect(receiver, &UdpReceiver::receiverBindingChanged, this, &UdpFramePipelineWorker::onReceiverBindingChanged, Qt::QueuedConnection);
    connect(receiverThread, &QThread::finished, receiver, &QObject::deleteLater);
    receiverThread->start();

    emit receiverSettingsChanged(receiverAddress, receiverPort);
    emit aiStatusChanged(aiStatusText);
    emit frameReady(displayBuffers[frontDisplayIndex]);
}

void UdpFramePipelineWorker::shutdown() {
    if (statsTimer != nullptr) {
        statsTimer->stop();
        statsTimer->deleteLater();
        statsTimer = nullptr;
    }

    if (receiver != nullptr) {
        QMetaObject::invokeMethod(receiver, "stopReceiving", Qt::BlockingQueuedConnection);
    }

    if (receiverThread != nullptr) {
        receiverThread->quit();
        receiverThread->wait();
        receiverThread = nullptr;
        receiver = nullptr;
    }

    if (yoloThread != nullptr) {
        yoloThread->quit();
        yoloThread->wait();
        yoloThread = nullptr;
        yoloProcessor = nullptr;
    }

    if (recorderWorker != nullptr) {
        QMetaObject::invokeMethod(recorderWorker, "stopRecording", Qt::BlockingQueuedConnection);
    }

    if (recorderThread != nullptr) {
        recorderThread->quit();
        recorderThread->wait();
        recorderThread = nullptr;
        recorderWorker = nullptr;
    }
}

bool UdpFramePipelineWorker::isMarkerPacket(const QByteArray &data, char marker) {
    if (data.size() <= kPacketHeaderSize) {
        return false;
    }

    const char *payload = data.constData() + kPacketHeaderSize;
    const char *payloadEnd = data.constData() + data.size();
    for (const char *it = payload; it != payloadEnd; ++it) {
        if (*it != marker) {
            return false;
        }
    }
    return true;
}

void UdpFramePipelineWorker::resetParserState(bool clearPendingQueue) {
    frameValid = false;
    currentLine = 0;
    linePayloadSizes.fill(0);
    std::fill(receivedLineFlags.begin(), receivedLineFlags.end(), static_cast<uchar>(0));

    if (clearPendingQueue) {
        QMutexLocker pendingLock(&pendingBatchMutex);
        pendingBatches.clear();
        pendingPacketCount = 0;
        parserResyncPending = false;
    }
}

void UdpFramePipelineWorker::saveSnapshot(const QString &directory) {
    if (directory.isEmpty()) {
        return;
    }

    QDir dir(directory);
    if (!dir.exists() && !dir.mkpath(".")) {
        emit receiverStatusChanged(QString("Snapshot save failed: cannot create %1").arg(directory));
        return;
    }

    const QString fileName = directory + "/snapshot_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
    if (!displayBuffers[frontDisplayIndex].save(fileName)) {
        emit receiverStatusChanged(QString("Snapshot save failed: %1").arg(fileName));
    }
}

void UdpFramePipelineWorker::toggleRecording(const QString &directory, const QString &format, int fps) {
    if (!isRecording) {
        emit startRecordingRequested(directory, format, fps, displayBuffers[frontDisplayIndex].size());
        emit recordFrameReady(displayBuffers[frontDisplayIndex]);
    } else {
        emit stopRecordingRequested();
    }
}

void UdpFramePipelineWorker::setFlipHorizontal(bool enabled) {
    if (flipHorizontal == enabled) {
        return;
    }
    flipHorizontal = enabled;
    refreshDisplayFromRaw();
}

void UdpFramePipelineWorker::setFlipVertical(bool enabled) {
    if (flipVertical == enabled) {
        return;
    }
    flipVertical = enabled;
    refreshDisplayFromRaw();
}

void UdpFramePipelineWorker::setBrightness(int value) {
    if (brightnessValue == value) {
        return;
    }
    brightnessValue = value;
    lutDirty = true;
    refreshDisplayFromRaw();
}

void UdpFramePipelineWorker::setGamma(int value) {
    if (gammaValue == value) {
        return;
    }
    gammaValue = value;
    lutDirty = true;
    refreshDisplayFromRaw();
}

void UdpFramePipelineWorker::setSharpness(int value) {
    if (sharpnessValue == value) {
        return;
    }
    sharpnessValue = value;
    refreshDisplayFromRaw();
}

void UdpFramePipelineWorker::setDenoise(int value) {
    if (denoiseValue == value) {
        return;
    }
    denoiseValue = value;
    refreshDisplayFromRaw();
}

void UdpFramePipelineWorker::applyReceiverSettings(const QString &address, quint16 port) {
    receiverAddress = address.trimmed();
    receiverPort = port;
    resetParserState(true);
    rawImage.fill(Qt::black);
    displayBuffers[0].fill(Qt::black);
    displayBuffers[1].fill(Qt::black);
    frontDisplayIndex = 0;
    latestDetections.clear();
    lastInferenceMs = 0;

    emit frameReady(displayBuffers[frontDisplayIndex]);
    emit receiverStatusChanged(QString("Rebinding receiver to %1:%2 ...").arg(receiverAddress).arg(receiverPort));
    emit receiverSettingsChanged(receiverAddress, receiverPort);

    if (receiver != nullptr) {
        QMetaObject::invokeMethod(receiver,
                                  "startReceiving",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, receiverAddress),
                                  Q_ARG(quint16, receiverPort));
    }
}

void UdpFramePipelineWorker::setAiDetectionEnabled(bool enabled) {
    aiDetectionEnabled = enabled;
    latestDetections.clear();
    lastInferenceMs = 0;

    if (yoloProcessor != nullptr) {
        QMetaObject::invokeMethod(yoloProcessor, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, enabled));
    }

    if (!enabled) {
        aiStatusText = "AI detection is disabled.";
        emit aiStatusChanged(aiStatusText);
        refreshDisplayFromRaw();
    }
}

void UdpFramePipelineWorker::onYoloDetectionsReady(const QVector<QRect> &boxes, int inferenceMs) {
    latestDetections = boxes;
    lastInferenceMs = inferenceMs;

    if (aiDetectionEnabled) {
        aiStatusText = QString("AI detection active | detections=%1 | infer=%2 ms")
                           .arg(latestDetections.size())
                           .arg(lastInferenceMs);
        emit aiStatusChanged(aiStatusText);
        refreshDisplayFromRaw();
    }
}

void UdpFramePipelineWorker::onYoloStatusChanged(const QString &statusText) {
    aiStatusText = statusText;
    emit aiStatusChanged(aiStatusText);
}

void UdpFramePipelineWorker::updateStats() {
    int currentQueuedPackets = 0;
    {
        QMutexLocker lock(&pendingBatchMutex);
        currentQueuedPackets = pendingPacketCount;
    }

    const double avgFrameMs = completedFramesThisSecond > 0
        ? static_cast<double>(frameProcessingNsThisSecond) / static_cast<double>(completedFramesThisSecond) / 1000000.0
        : 0.0;
    const double avgInterpolationMs = completedFramesThisSecond > 0
        ? static_cast<double>(interpolationNsThisSecond) / static_cast<double>(completedFramesThisSecond) / 1000000.0
        : 0.0;
    const double drainMsThisSecond = static_cast<double>(drainNsThisSecond) / 1000000.0;
    const double maxDrainMs = static_cast<double>(maxDrainNsThisSecond) / 1000000.0;
    const QString statsText = QString("Perf: pkts/s=%1 | parse fps=%2 | frame=%3 ms | interp=%4 ms | drain=%5 ms/s | drain max=%6 ms | recovered lines/s=%7\nmarkers start/end=%8/%9 | start-no-end=%10 | end-no-start=%11 | orphan=%12 | overflow=%13 | short-end=%14 | resync=%15\nqueue cur=%16 | dropped pkts/s=%17 | dropped batches/s=%18 | queue max=%19/%20\nproc flipH=%21 | flipV=%22 | bright=%23 | gamma=%24 | sharp=%25 | denoise=%26\nai enabled=%27 | det=%28 | infer=%29 ms")
                                  .arg(datagramsThisSecond)
                                  .arg(completedFramesThisSecond)
                                  .arg(avgFrameMs, 0, 'f', 3)
                                  .arg(avgInterpolationMs, 0, 'f', 3)
                                  .arg(drainMsThisSecond, 0, 'f', 3)
                                  .arg(maxDrainMs, 0, 'f', 3)
                                  .arg(recoveredLinesThisSecond)
                                  .arg(startMarkersThisSecond)
                                  .arg(endMarkersThisSecond)
                                  .arg(startWithoutEndThisSecond)
                                  .arg(endWithoutStartThisSecond)
                                  .arg(orphanLinePacketsThisSecond)
                                  .arg(overflowLinePacketsThisSecond)
                                  .arg(shortFrameEndsThisSecond)
                                  .arg(parserResyncEventsThisSecond)
                                  .arg(currentQueuedPackets)
                                  .arg(droppedPacketsThisSecond)
                                  .arg(droppedBatchesThisSecond)
                                  .arg(maxQueuedPacketsThisSecond)
                                  .arg(kMaxQueuedPackets)
                                  .arg(flipHorizontal ? 1 : 0)
                                  .arg(flipVertical ? 1 : 0)
                                  .arg(brightnessValue)
                                  .arg(gammaValue)
                                  .arg(sharpnessValue)
                                  .arg(denoiseValue)
                                  .arg(aiDetectionEnabled ? 1 : 0)
                                  .arg(latestDetections.size())
                                  .arg(lastInferenceMs);
    emit statsReady(statsText);

    datagramsThisSecond = 0;
    completedFramesThisSecond = 0;
    recoveredLinesThisSecond = 0;
    frameProcessingNsThisSecond = 0;
    interpolationNsThisSecond = 0;
    droppedPacketsThisSecond = 0;
    droppedBatchesThisSecond = 0;
    maxQueuedPacketsThisSecond = 0;
    drainNsThisSecond = 0;
    maxDrainNsThisSecond = 0;
    startMarkersThisSecond = 0;
    endMarkersThisSecond = 0;
    startWithoutEndThisSecond = 0;
    endWithoutStartThisSecond = 0;
    orphanLinePacketsThisSecond = 0;
    overflowLinePacketsThisSecond = 0;
    shortFrameEndsThisSecond = 0;
    parserResyncEventsThisSecond = 0;
}

void UdpFramePipelineWorker::enqueueFrameBatch(const QList<QByteArray> &batch) {
    if (batch.isEmpty()) {
        return;
    }

    bool shouldScheduleDrain = false;
    {
        QMutexLocker lock(&pendingBatchMutex);
        pendingBatches.enqueue(batch);
        pendingPacketCount += batch.size();
        maxQueuedPacketsThisSecond = std::max(maxQueuedPacketsThisSecond, static_cast<quint64>(pendingPacketCount));

        while (pendingPacketCount > kMaxQueuedPackets && !pendingBatches.isEmpty()) {
            const QList<QByteArray> droppedBatch = pendingBatches.dequeue();
            pendingPacketCount -= droppedBatch.size();
            droppedPacketsThisSecond += droppedBatch.size();
            ++droppedBatchesThisSecond;
            parserResyncPending = true;
        }

        if (!drainScheduled) {
            drainScheduled = true;
            shouldScheduleDrain = true;
        }
    }

    if (shouldScheduleDrain) {
        QMetaObject::invokeMethod(this, "drainPendingBatches", Qt::QueuedConnection);
    }
}

void UdpFramePipelineWorker::drainPendingBatches() {
    QElapsedTimer drainTimer;
    drainTimer.start();

    while (true) {
        QList<QByteArray> batch;
        bool needResync = false;
        {
            QMutexLocker lock(&pendingBatchMutex);
            if (pendingBatches.isEmpty()) {
                drainScheduled = false;
                break;
            }

            batch = pendingBatches.dequeue();
            pendingPacketCount -= batch.size();
            needResync = parserResyncPending;
            parserResyncPending = false;
        }

        if (needResync) {
            resetParserState(false);
            ++parserResyncEventsThisSecond;
        }

        for (QList<QByteArray>::const_iterator it = batch.cbegin(); it != batch.cend(); ++it) {
            processFrameData(*it);
        }
    }

    const quint64 elapsedNs = static_cast<quint64>(drainTimer.nsecsElapsed());
    drainNsThisSecond += elapsedNs;
    maxDrainNsThisSecond = std::max(maxDrainNsThisSecond, elapsedNs);
}

void UdpFramePipelineWorker::onReceiverBindingChanged(const QString &address, quint16 port, bool ok, const QString &message) {
    Q_UNUSED(address);
    Q_UNUSED(port);
    emit receiverStatusChanged(ok ? message : QString("Receiver error: %1").arg(message));
}

void UdpFramePipelineWorker::processFrameData(const QByteArray &data) {
    ++datagramsThisSecond;

    if (data.size() < kPacketHeaderSize) {
        return;
    }

    if (isMarkerPacket(data, char(0xAA))) {
        ++startMarkersThisSecond;
        if (frameValid) {
            ++startWithoutEndThisSecond;
        }
        frameValid = true;
        currentLine = 0;
        linePayloadSizes.fill(0);
        std::fill(receivedLineFlags.begin(), receivedLineFlags.end(), static_cast<uchar>(0));
        return;
    }

    if (isMarkerPacket(data, char(0xBB))) {
        ++endMarkersThisSecond;
        if (frameValid) {
            if (currentLine < kFrameHeight) {
                ++shortFrameEndsThisSecond;
            }
            finalizeFrame();
        } else {
            ++endWithoutStartThisSecond;
        }
        frameValid = false;
        currentLine = 0;
        return;
    }

    if (!frameValid) {
        ++orphanLinePacketsThisSecond;
        return;
    }

    if (currentLine < 0 || currentLine >= kFrameHeight) {
        ++overflowLinePacketsThisSecond;
        return;
    }

    const int payloadSize = data.size() - kPacketHeaderSize;
    const int copySize = std::min(payloadSize, kLinePayloadBytes);
    char *lineBuffer = frameData.data() + (currentLine * kLinePayloadBytes);
    std::memcpy(lineBuffer, data.constData() + kPacketHeaderSize, static_cast<size_t>(copySize));
    if (copySize < kLinePayloadBytes) {
        std::memset(lineBuffer + copySize, 0, static_cast<size_t>(kLinePayloadBytes - copySize));
    }
    linePayloadSizes[currentLine] = copySize;
    receivedLineFlags[currentLine] = 1;
    ++currentLine;
}

void UdpFramePipelineWorker::averageLineBytes(char *destLine, const char *topLine, const char *bottomLine, int length) const {
    int index = 0;
    const int simdLength = length - (length % 16);
    for (; index < simdLength; index += 16) {
        const __m128i top = _mm_loadu_si128(reinterpret_cast<const __m128i *>(topLine + index));
        const __m128i bottom = _mm_loadu_si128(reinterpret_cast<const __m128i *>(bottomLine + index));
        const __m128i avg = _mm_avg_epu8(top, bottom);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(destLine + index), avg);
    }

    for (; index < length; ++index) {
        destLine[index] = static_cast<char>((static_cast<unsigned char>(topLine[index]) + static_cast<unsigned char>(bottomLine[index])) / 2);
    }
}

void UdpFramePipelineWorker::finalizeFrame() {
    QElapsedTimer frameTimer;
    frameTimer.start();

    QElapsedTimer interpolationTimer;
    interpolationTimer.start();
    quint64 recoveredLines = 0;

#ifdef _OPENMP
#pragma omp parallel for reduction(+:recoveredLines) schedule(static)
#endif
    for (int i = 0; i < kFrameHeight; ++i) {
        if (receivedLineFlags[i]) {
            continue;
        }

        ++recoveredLines;
        const int topIndex = (i > 0 && receivedLineFlags[i - 1]) ? (i - 1) : -1;
        const int bottomIndex = (i + 1 < kFrameHeight && receivedLineFlags[i + 1]) ? (i + 1) : -1;
        char *destLine = frameData.data() + (i * kLinePayloadBytes);

        if (topIndex >= 0 && bottomIndex >= 0) {
            const char *topLine = frameData.constData() + (topIndex * kLinePayloadBytes);
            const char *bottomLine = frameData.constData() + (bottomIndex * kLinePayloadBytes);
            averageLineBytes(destLine, topLine, bottomLine, kLinePayloadBytes);
            linePayloadSizes[i] = kLinePayloadBytes;
        } else if (topIndex >= 0) {
            const char *topLine = frameData.constData() + (topIndex * kLinePayloadBytes);
            std::memcpy(destLine, topLine, static_cast<size_t>(kLinePayloadBytes));
            linePayloadSizes[i] = linePayloadSizes[topIndex];
        } else if (bottomIndex >= 0) {
            const char *bottomLine = frameData.constData() + (bottomIndex * kLinePayloadBytes);
            std::memcpy(destLine, bottomLine, static_cast<size_t>(kLinePayloadBytes));
            linePayloadSizes[i] = linePayloadSizes[bottomIndex];
        } else {
            std::memset(destLine, 0, static_cast<size_t>(kLinePayloadBytes));
            linePayloadSizes[i] = 0;
        }
        receivedLineFlags[i] = 1;
    }

    interpolationNsThisSecond += static_cast<quint64>(interpolationTimer.nsecsElapsed());
    recoveredLinesThisSecond += recoveredLines;

    const int rawBytesPerLine = rawImage.bytesPerLine();
    uchar *rawBits = rawImage.bits();

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < kFrameHeight; ++i) {
        uchar *imageBits = rawBits + (i * rawBytesPerLine);
        if (linePayloadSizes[i] <= 0) {
            std::memset(imageBits, 0, static_cast<size_t>(rawBytesPerLine));
            continue;
        }

        const char *lineData = frameData.constData() + (i * kLinePayloadBytes);
        const int pixelCount = linePayloadSizes[i] / 2;
        for (int j = 0; j < pixelCount; ++j) {
            const unsigned char high = static_cast<unsigned char>(lineData[j * 2]);
            const unsigned char low = static_cast<unsigned char>(lineData[(j * 2) + 1]);
            const quint16 rgb565 = static_cast<quint16>((high << 8) | low);

            imageBits[j * 3] = kExpand5To8[static_cast<size_t>((rgb565 >> 11) & 0x1F)];
            imageBits[(j * 3) + 1] = kExpand6To8[static_cast<size_t>((rgb565 >> 5) & 0x3F)];
            imageBits[(j * 3) + 2] = kExpand5To8[static_cast<size_t>(rgb565 & 0x1F)];
        }

        if (pixelCount * 3 < rawBytesPerLine) {
            std::memset(imageBits + (pixelCount * 3), 0, static_cast<size_t>(rawBytesPerLine - (pixelCount * 3)));
        }
    }

    composeDisplayFrame(true);

    ++completedFramesThisSecond;
    frameProcessingNsThisSecond += static_cast<quint64>(frameTimer.nsecsElapsed());
}

void UdpFramePipelineWorker::copyRawToDisplay(QImage &dest) {
    const int rowBytes = rawImage.bytesPerLine();
    const uchar *srcBits = rawImage.constBits();
    uchar *dstBits = dest.bits();

    if (!flipHorizontal && !flipVertical) {
        std::memcpy(dstBits, srcBits, static_cast<size_t>(rowBytes * kFrameHeight));
        return;
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int y = 0; y < kFrameHeight; ++y) {
        const int srcY = flipVertical ? (kFrameHeight - 1 - y) : y;
        const uchar *srcRow = srcBits + (srcY * rowBytes);
        uchar *dstRow = dstBits + (y * rowBytes);

        if (!flipHorizontal) {
            std::memcpy(dstRow, srcRow, static_cast<size_t>(rowBytes));
            continue;
        }

        for (int x = 0; x < kFrameWidth; ++x) {
            const int srcX = kFrameWidth - 1 - x;
            const uchar *srcPixel = srcRow + (srcX * 3);
            uchar *dstPixel = dstRow + (x * 3);
            dstPixel[0] = srcPixel[0];
            dstPixel[1] = srcPixel[1];
            dstPixel[2] = srcPixel[2];
        }
    }
}

void UdpFramePipelineWorker::flipImageInPlace(QImage &image) const {
    if (!flipHorizontal && !flipVertical) {
        return;
    }

    const int rowBytes = image.bytesPerLine();
    uchar *bits = image.bits();

    if (flipVertical) {
        QByteArray tempRow(rowBytes, 0);
        for (int y = 0; y < kFrameHeight / 2; ++y) {
            uchar *topRow = bits + (y * rowBytes);
            uchar *bottomRow = bits + ((kFrameHeight - 1 - y) * rowBytes);
            std::memcpy(tempRow.data(), topRow, static_cast<size_t>(rowBytes));
            std::memcpy(topRow, bottomRow, static_cast<size_t>(rowBytes));
            std::memcpy(bottomRow, tempRow.constData(), static_cast<size_t>(rowBytes));
        }
    }

    if (flipHorizontal) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int y = 0; y < kFrameHeight; ++y) {
            uchar *row = bits + (y * rowBytes);
            for (int left = 0, right = kFrameWidth - 1; left < right; ++left, --right) {
                uchar *leftPixel = row + (left * 3);
                uchar *rightPixel = row + (right * 3);
                std::swap(leftPixel[0], rightPixel[0]);
                std::swap(leftPixel[1], rightPixel[1]);
                std::swap(leftPixel[2], rightPixel[2]);
            }
        }
    }
}

QRect UdpFramePipelineWorker::transformDetectionRect(const QRect &rect) const {
    QRect mapped = rect;
    if (flipHorizontal) {
        mapped.setX(kFrameWidth - rect.x() - rect.width());
    }
    if (flipVertical) {
        mapped.setY(kFrameHeight - rect.y() - rect.height());
    }
    return mapped;
}

void UdpFramePipelineWorker::drawDetections(QImage &image) const {
    if (!aiDetectionEnabled || latestDetections.isEmpty()) {
        return;
    }

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(QColor(255, 90, 50));
    pen.setWidth(3);
    painter.setPen(pen);

    for (int i = 0; i < latestDetections.size(); ++i) {
        painter.drawRect(transformDetectionRect(latestDetections[i]));
    }
}

void UdpFramePipelineWorker::rebuildToneLutIfNeeded() {
    if (!lutDirty) {
        return;
    }

    const int brightnessOffset = brightnessValue - 50;
    const double gammaExponent = std::pow(2.0, -static_cast<double>(gammaValue) / 100.0);
    for (int i = 0; i < 256; ++i) {
        const double normalized = static_cast<double>(clampValue(i + brightnessOffset, 0, 255)) / 255.0;
        const double corrected = std::pow(normalized, gammaExponent);
        toneLut[static_cast<size_t>(i)] = static_cast<uchar>(clampValue(corrected * 255.0, 0.0, 255.0));
    }

    lutDirty = false;
}

void UdpFramePipelineWorker::applyProcessing(const cv::Mat &sourceRgb, cv::Mat &destRgb) {
    rebuildToneLutIfNeeded();
    sourceRgb.copyTo(destRgb);

    cv::Mat lut(1, 256, CV_8U, toneLut.data());
    cv::LUT(destRgb, lut, destRgb);

    if (denoiseValue > 0) {
        const int kernelRadius = std::max(1, denoiseValue / 20);
        const int kernelSize = (kernelRadius * 2) + 1;
        cv::GaussianBlur(destRgb, destRgb, cv::Size(kernelSize, kernelSize), 0.0, 0.0, cv::BORDER_REPLICATE);
    }

    if (sharpnessValue > 0) {
        cv::Mat blurred;
        cv::GaussianBlur(destRgb, blurred, cv::Size(0, 0), 1.2, 1.2, cv::BORDER_REPLICATE);
        const double amount = static_cast<double>(sharpnessValue) / 50.0;
        cv::addWeighted(destRgb, 1.0 + amount, blurred, -amount, 0.0, destRgb);
    }
}

void UdpFramePipelineWorker::composeDisplayFrame(bool submitAiFrame) {
    const int backIndex = 1 - frontDisplayIndex;
    QImage &backBuffer = displayBuffers[backIndex];

    if (brightnessValue == 50 && gammaValue == 0 && sharpnessValue == 0 && denoiseValue == 0) {
        copyRawToDisplay(backBuffer);
    } else {
        cv::Mat sourceRgb(rawImage.height(), rawImage.width(), CV_8UC3, rawImage.bits(), rawImage.bytesPerLine());
        cv::Mat destRgb(backBuffer.height(), backBuffer.width(), CV_8UC3, backBuffer.bits(), backBuffer.bytesPerLine());
        applyProcessing(sourceRgb, destRgb);
        flipImageInPlace(backBuffer);
    }

    drawDetections(backBuffer);

    frontDisplayIndex = backIndex;
    emit frameReady(displayBuffers[frontDisplayIndex]);
    if (isRecording) {
        emit recordFrameReady(displayBuffers[frontDisplayIndex]);
    }

    if (submitAiFrame && aiDetectionEnabled && yoloProcessor != nullptr) {
        QMetaObject::invokeMethod(yoloProcessor,
                                  "submitFrame",
                                  Qt::QueuedConnection,
                                  Q_ARG(QImage, rawImage.copy()));
    }
}

void UdpFramePipelineWorker::refreshDisplayFromRaw() {
    composeDisplayFrame(false);
}
