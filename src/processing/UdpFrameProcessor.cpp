#include "UdpFrameProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {
const int kPacketHeaderSize = 4;
const int kFrameWidth = 400;
const int kFrameHeight = 400;
const int kLinePayloadBytes = kFrameWidth * 2;
const int kPacketsPerFrame = kFrameHeight + 2;
const int kMaxQueuedFrames = 6;
const int kMaxQueuedPackets = kPacketsPerFrame * kMaxQueuedFrames;

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
}

UdpFrameProcessor::UdpFrameProcessor(QWidget *parent)
    : QWidget(parent),
      presentTimer(nullptr),
      frameCount(0),
      presentedFrameCount(0),
      datagramsThisSecond(0),
      completedFramesThisSecond(0),
      recoveredLinesThisSecond(0),
      frameProcessingNsThisSecond(0),
      interpolationNsThisSecond(0),
      droppedPacketsThisSecond(0),
      droppedBatchesThisSecond(0),
      maxQueuedPacketsThisSecond(0),
      startMarkersThisSecond(0),
      endMarkersThisSecond(0),
      startWithoutEndThisSecond(0),
      endWithoutStartThisSecond(0),
      orphanLinePacketsThisSecond(0),
      overflowLinePacketsThisSecond(0),
      shortFrameEndsThisSecond(0),
      parserResyncEventsThisSecond(0),
      currentLine(0),
      frameValid(false),
      frameData(kFrameHeight * kLinePayloadBytes, 0),
      linePayloadSizes(kFrameHeight, 0),
      receivedLineFlags(kFrameHeight, false),
      pendingPacketCount(0),
      drainScheduled(false),
      parserResyncPending(false),
      receiver(nullptr),
      receiverThread(nullptr),
      flipHorizontal(false),
      flipVertical(false),
      brightnessValue(50),
      gammaValue(0),
      sharpnessValue(0),
      denoiseValue(0),
      receiverAddress("0.0.0.0"),
      receiverPort(8080),
      aiDetectionEnabled(false),
      isRecording(false),
      framePendingPresentation(false) {
    rawImage = QImage(kFrameWidth, kFrameHeight, QImage::Format_RGB888);
    rawImage.fill(Qt::black);
    displayImage = rawImage.copy();

    setMinimumSize(960, 960);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    fpsTimer = new QTimer(this);
    connect(fpsTimer, &QTimer::timeout, this, &UdpFrameProcessor::updateFPS);
    fpsTimer->start(1000);

    presentTimer = new QTimer(this);
    presentTimer->setTimerType(Qt::PreciseTimer);
    connect(presentTimer, &QTimer::timeout, this, &UdpFrameProcessor::presentLatestFrame);
    presentTimer->start(16);

    receiver = new UdpReceiver();
    receiverThread = new QThread();
    receiver->moveToThread(receiverThread);
    connect(receiverThread, &QThread::started, this, [this]() {
        QMetaObject::invokeMethod(receiver,
                                  "startReceiving",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, receiverAddress),
                                  Q_ARG(quint16, receiverPort));
    });
    connect(receiver, &UdpReceiver::newFrameBatch, this, &UdpFrameProcessor::enqueueFrameBatch, Qt::DirectConnection);
    connect(receiver,
            &UdpReceiver::receiverBindingChanged,
            this,
            &UdpFrameProcessor::onReceiverBindingChanged,
            Qt::QueuedConnection);
    connect(receiverThread, &QThread::finished, receiverThread, &QObject::deleteLater);
    receiverThread->start();

    emit receiverSettingsChanged(receiverAddress, receiverPort);
    emit aiStatusChanged("AI detection is disabled.");
}

UdpFrameProcessor::~UdpFrameProcessor() {
    receiverThread->quit();
    receiverThread->wait();
    delete receiver;

    if (videoWriter.isOpened()) {
        videoWriter.release();
    }
}

bool UdpFrameProcessor::isMarkerPacket(const QByteArray &data, char marker) {
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

void UdpFrameProcessor::applyProcessing(const cv::Mat &sourceRgb, cv::Mat &destRgb) const {
    destRgb = sourceRgb.clone();

    std::array<uchar, 256> lutTable = {};
    const int brightnessOffset = brightnessValue - 50;
    const double gammaExponent = std::pow(2.0, -static_cast<double>(gammaValue) / 100.0);
    for (int i = 0; i < 256; ++i) {
        const double normalized = static_cast<double>(clampValue(i + brightnessOffset, 0, 255)) / 255.0;
        const double corrected = std::pow(normalized, gammaExponent);
        lutTable[static_cast<size_t>(i)] = static_cast<uchar>(clampValue(corrected * 255.0, 0.0, 255.0));
    }

    cv::Mat lut(1, 256, CV_8U, lutTable.data());
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

void UdpFrameProcessor::refreshDisplayImage() {
    if (brightnessValue == 50 && gammaValue == 0 && sharpnessValue == 0 && denoiseValue == 0) {
        QMutexLocker lock(&imageMutex);
        displayImage = rawImage;
        return;
    }

    QImage rawCopy;
    {
        QMutexLocker lock(&imageMutex);
        rawCopy = rawImage.copy();
    }

    cv::Mat sourceRgb(rawCopy.height(), rawCopy.width(), CV_8UC3, rawCopy.bits(), rawCopy.bytesPerLine());
    cv::Mat processedRgb;
    applyProcessing(sourceRgb, processedRgb);

    QImage processedImage(processedRgb.data, processedRgb.cols, processedRgb.rows, processedRgb.step, QImage::Format_RGB888);
    {
        QMutexLocker lock(&imageMutex);
        displayImage = processedImage.copy();
    }
}

void UdpFrameProcessor::writeFrameToVideo() {
    QImage frameCopy = getCurrentFrame();
    cv::Mat mat(frameCopy.height(), frameCopy.width(), CV_8UC3, frameCopy.bits(), frameCopy.bytesPerLine());
    cv::Mat matBGR;
    cv::cvtColor(mat, matBGR, cv::COLOR_RGB2BGR);

    if (!matBGR.empty()) {
        videoWriter.write(matBGR);
    } else {
        qWarning() << "Frame data is empty or invalid. Skipping frame.";
    }
}

void UdpFrameProcessor::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QImage frameToDraw;
    const bool mirrorHorizontal = flipHorizontal;
    const bool mirrorVertical = flipVertical;
    {
        QMutexLocker lock(&imageMutex);
        frameToDraw = displayImage;
    }

    if (frameToDraw.isNull()) {
        return;
    }

    if (mirrorHorizontal || mirrorVertical) {
        frameToDraw = frameToDraw.mirrored(mirrorHorizontal, mirrorVertical);
    }

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    const QSize drawSize = frameToDraw.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect targetRect(QPoint((width() - drawSize.width()) / 2, (height() - drawSize.height()) / 2), drawSize);

    painter.drawImage(targetRect, frameToDraw);
}

void UdpFrameProcessor::updateFPS() {
    emit fpsChanged(presentedFrameCount);

    const double avgFrameMs = completedFramesThisSecond > 0
        ? static_cast<double>(frameProcessingNsThisSecond) / static_cast<double>(completedFramesThisSecond) / 1000000.0
        : 0.0;
    const double avgInterpolationMs = completedFramesThisSecond > 0
        ? static_cast<double>(interpolationNsThisSecond) / static_cast<double>(completedFramesThisSecond) / 1000000.0
        : 0.0;
    const QString finalStatsText = QString("Perf: pkts/s=%1 | parse fps=%2 | present fps=%3 | frame=%4 ms | interp=%5 ms | recovered lines/s=%6\nmarkers start/end=%7/%8 | start-no-end=%9 | end-no-start=%10 | orphan=%11 | overflow=%12 | short-end=%13 | resync=%14\nqueue dropped pkts/s=%15 | queue max=%16/%17")
                                  .arg(datagramsThisSecond)
                                  .arg(frameCount)
                                  .arg(presentedFrameCount)
                                  .arg(avgFrameMs, 0, 'f', 3)
                                  .arg(avgInterpolationMs, 0, 'f', 3)
                                  .arg(recoveredLinesThisSecond)
                                  .arg(startMarkersThisSecond)
                                  .arg(endMarkersThisSecond)
                                  .arg(startWithoutEndThisSecond)
                                  .arg(endWithoutStartThisSecond)
                                  .arg(orphanLinePacketsThisSecond)
                                  .arg(overflowLinePacketsThisSecond)
                                  .arg(shortFrameEndsThisSecond)
                                  .arg(parserResyncEventsThisSecond)
                                  .arg(droppedPacketsThisSecond)
                                  .arg(maxQueuedPacketsThisSecond)
                                  .arg(kMaxQueuedPackets);
    emit performanceStatsChanged(finalStatsText);

    frameCount = 0;
    presentedFrameCount = 0;
    datagramsThisSecond = 0;
    completedFramesThisSecond = 0;
    recoveredLinesThisSecond = 0;
    frameProcessingNsThisSecond = 0;
    interpolationNsThisSecond = 0;
    droppedPacketsThisSecond = 0;
    droppedBatchesThisSecond = 0;
    maxQueuedPacketsThisSecond = 0;
    startMarkersThisSecond = 0;
    endMarkersThisSecond = 0;
    startWithoutEndThisSecond = 0;
    endWithoutStartThisSecond = 0;
    orphanLinePacketsThisSecond = 0;
    overflowLinePacketsThisSecond = 0;
    shortFrameEndsThisSecond = 0;
    parserResyncEventsThisSecond = 0;
}

void UdpFrameProcessor::presentLatestFrame() {
    if (!framePendingPresentation) {
        return;
    }

    framePendingPresentation = false;
    ++presentedFrameCount;
    update();
}

void UdpFrameProcessor::resetParserState() {
    frameValid = false;
    currentLine = 0;
    linePayloadSizes.fill(0);
    receivedLineFlags.fill(false);

    QMutexLocker pendingLock(&pendingBatchMutex);
    pendingBatches.clear();
    pendingPacketCount = 0;
    parserResyncPending = false;
}

void UdpFrameProcessor::enqueueFrameBatch(const QList<QByteArray> &batch) {
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

void UdpFrameProcessor::drainPendingBatches() {
    while (true) {
        QList<QByteArray> batch;
        bool needResync = false;
        {
            QMutexLocker lock(&pendingBatchMutex);
            if (pendingBatches.isEmpty()) {
                drainScheduled = false;
                return;
            }

            batch = pendingBatches.dequeue();
            pendingPacketCount -= batch.size();
            needResync = parserResyncPending;
            parserResyncPending = false;
        }

        if (needResync) {
            frameValid = false;
            currentLine = 0;
            linePayloadSizes.fill(0);
            receivedLineFlags.fill(false);
            ++parserResyncEventsThisSecond;
        }

        for (QList<QByteArray>::const_iterator it = batch.cbegin(); it != batch.cend(); ++it) {
            processFrameData(*it);
        }
    }
}

void UdpFrameProcessor::onReceiverBindingChanged(const QString &address, quint16 port, bool ok, const QString &message) {
    Q_UNUSED(address);
    Q_UNUSED(port);
    emit receiverStatusChanged(ok ? message : QString("Receiver error: %1").arg(message));
}

void UdpFrameProcessor::processFrameData(const QByteArray &data) {
    ++datagramsThisSecond;

    if (data.size() < kPacketHeaderSize) {
        qWarning() << "Incomplete packet received. Packet too small:" << data.size();
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
        receivedLineFlags.fill(false);
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
    receivedLineFlags[currentLine] = true;
    ++currentLine;
}

void UdpFrameProcessor::finalizeFrame() {
    QElapsedTimer frameTimer;
    frameTimer.start();

    QElapsedTimer interpolationTimer;
    interpolationTimer.start();
    quint64 recoveredLines = 0;

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
            for (int j = 0; j < kLinePayloadBytes; ++j) {
                destLine[j] = static_cast<char>((static_cast<unsigned char>(topLine[j]) + static_cast<unsigned char>(bottomLine[j])) / 2);
            }
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
    }

    interpolationNsThisSecond += static_cast<quint64>(interpolationTimer.nsecsElapsed());
    recoveredLinesThisSecond += recoveredLines;

    {
        QMutexLocker lock(&imageMutex);
        for (int i = 0; i < kFrameHeight; ++i) {
            if (linePayloadSizes[i] <= 0) {
                continue;
            }

            uchar *imageBits = rawImage.bits() + (i * rawImage.bytesPerLine());
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
        }
    }

    refreshDisplayImage();

    ++frameCount;
    ++completedFramesThisSecond;
    frameProcessingNsThisSecond += static_cast<quint64>(frameTimer.nsecsElapsed());
    framePendingPresentation = true;

    if (isRecording && videoWriter.isOpened()) {
        writeFrameToVideo();
    }
}

QImage UdpFrameProcessor::getCurrentFrame() {
    QMutexLocker lock(&imageMutex);
    return buildOutputFrame(displayImage);
}

void UdpFrameProcessor::saveSnapshot(const QString &directory) {
    if (!directory.isEmpty()) {
        QString fileName = directory + "/snapshot_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
        if (getCurrentFrame().save(fileName)) {
            qDebug() << "Snapshot saved to" << fileName;
        } else {
            qWarning() << "Failed to save snapshot.";
        }
    } else {
        qWarning() << "Save directory is not set.";
    }
}

void UdpFrameProcessor::toggleRecording(const QString &directory, const QString &format, int fps) {
    if (!isRecording) {
        QDir dir(directory);
        if (!dir.exists() && !dir.mkpath(".")) {
            qWarning() << "Failed to create directory:" << directory;
            emit recordingStateChanged(false);
            return;
        }

        const QString extension = format.toLower();
        const QString fileName = directory + "/recording_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "." + extension;
        const int codec = (extension == "avi")
            ? cv::VideoWriter::fourcc('M', 'J', 'P', 'G')
            : cv::VideoWriter::fourcc('m', 'p', '4', 'v');

        const QImage frameCopy = getCurrentFrame();
        if (frameCopy.isNull()) {
            qWarning() << "Current frame is empty. Cannot start recording.";
            emit recordingStateChanged(false);
            return;
        }

        try {
            videoWriter.open(fileName.toStdString(), codec, fps, cv::Size(frameCopy.width(), frameCopy.height()));
        } catch (const cv::Exception &e) {
            qWarning() << "OpenCV exception while opening VideoWriter:" << e.what();
            emit recordingStateChanged(false);
            return;
        }

        if (!videoWriter.isOpened()) {
            qWarning() << "Failed to open VideoWriter. Check codec, resolution, or file permissions.";
            emit recordingStateChanged(false);
            return;
        }

        isRecording = true;
        emit recordingStateChanged(true);
        writeFrameToVideo();
    } else {
        if (videoWriter.isOpened()) {
            videoWriter.release();
        }

        isRecording = false;
        emit recordingStateChanged(false);
    }
}

void UdpFrameProcessor::setFlipHorizontal(bool enabled) {
    flipHorizontal = enabled;
    update();
}

void UdpFrameProcessor::setFlipVertical(bool enabled) {
    flipVertical = enabled;
    update();
}

void UdpFrameProcessor::setBrightness(int value) {
    brightnessValue = value;
    refreshDisplayImage();
    update();
}

void UdpFrameProcessor::setGamma(int value) {
    gammaValue = value;
    refreshDisplayImage();
    update();
}

void UdpFrameProcessor::setSharpness(int value) {
    sharpnessValue = value;
    refreshDisplayImage();
    update();
}

void UdpFrameProcessor::setDenoise(int value) {
    denoiseValue = value;
    refreshDisplayImage();
    update();
}

void UdpFrameProcessor::applyReceiverSettings(const QString &address, quint16 port) {
    receiverAddress = address.trimmed();
    receiverPort = port;
    resetParserState();

    {
        QMutexLocker imageLock(&imageMutex);
        rawImage.fill(Qt::black);
        displayImage = rawImage.copy();
    }

    emit receiverStatusChanged(QString("Rebinding receiver to %1:%2 ...").arg(receiverAddress).arg(receiverPort));
    emit receiverSettingsChanged(receiverAddress, receiverPort);

    QMetaObject::invokeMethod(receiver,
                              "startReceiving",
                              Qt::QueuedConnection,
                              Q_ARG(QString, receiverAddress),
                              Q_ARG(quint16, receiverPort));
    update();
}

void UdpFrameProcessor::setAiDetectionEnabled(bool enabled) {
    aiDetectionEnabled = enabled;

    if (enabled) {
        emit aiStatusChanged("AI detection armed. No model is configured yet, so inference stays idle.");
    } else {
        emit aiStatusChanged("AI detection is disabled.");
    }
}

QImage UdpFrameProcessor::buildOutputFrame(const QImage &sourceFrame) const {
    if (sourceFrame.isNull()) {
        return QImage();
    }

    if (!flipHorizontal && !flipVertical) {
        return sourceFrame;
    }

    return sourceFrame.mirrored(flipHorizontal, flipVertical);
}
