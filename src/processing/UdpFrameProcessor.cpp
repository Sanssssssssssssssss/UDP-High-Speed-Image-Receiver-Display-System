#include "UdpFrameProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {
const int kPacketHeaderSize = 4;
const int kFrameWidth = 400;
const int kFrameHeight = 400;
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
      frameCount(0),
      datagramsThisSecond(0),
      completedFramesThisSecond(0),
      recoveredLinesThisSecond(0),
      frameProcessingNsThisSecond(0),
      interpolationNsThisSecond(0),
      droppedPacketsThisSecond(0),
      droppedBatchesThisSecond(0),
      maxQueuedPacketsThisSecond(0),
      currentLine(0),
      frameValid(false),
      frameBuffer(kFrameHeight),
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
      isRecording(false) {
    rawImage = QImage(kFrameWidth, kFrameHeight, QImage::Format_RGB888);
    rawImage.fill(Qt::black);
    displayImage = rawImage.copy();

    setMinimumSize(960, 960);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    fpsTimer = new QTimer(this);
    connect(fpsTimer, &QTimer::timeout, this, &UdpFrameProcessor::updateFPS);
    fpsTimer->start(1000);

    receiver = new UdpReceiver();
    receiverThread = new QThread();
    receiver->moveToThread(receiverThread);
    connect(receiverThread, &QThread::started, receiver, [=]() { receiver->startReceiving("0.0.0.0", 8080); });
    connect(receiver, &UdpReceiver::newFrameBatch, this, &UdpFrameProcessor::enqueueFrameBatch, Qt::DirectConnection);
    connect(receiverThread, &QThread::finished, receiverThread, &QObject::deleteLater);
    receiverThread->start();
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
    QImage rawCopy;
    {
        QMutexLocker lock(&imageMutex);
        rawCopy = rawImage.copy();
    }

    if (brightnessValue == 50 && gammaValue == 0 && sharpnessValue == 0 && denoiseValue == 0) {
        QMutexLocker lock(&imageMutex);
        displayImage = rawCopy;
        return;
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
    {
        QMutexLocker lock(&imageMutex);
        frameToDraw = buildOutputFrame(displayImage);
    }

    if (frameToDraw.isNull()) {
        return;
    }

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    const QSize drawSize = frameToDraw.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect targetRect(QPoint((width() - drawSize.width()) / 2, (height() - drawSize.height()) / 2), drawSize);

    painter.drawImage(targetRect, frameToDraw);
}

void UdpFrameProcessor::updateFPS() {
    emit fpsChanged(frameCount);

    const double avgFrameMs = completedFramesThisSecond > 0
        ? static_cast<double>(frameProcessingNsThisSecond) / static_cast<double>(completedFramesThisSecond) / 1000000.0
        : 0.0;
    const double avgInterpolationMs = completedFramesThisSecond > 0
        ? static_cast<double>(interpolationNsThisSecond) / static_cast<double>(completedFramesThisSecond) / 1000000.0
        : 0.0;
    const QString statsText = QString("Perf: pkts/s=%1 | frame=%2 ms | interp=%3 ms | recovered lines/s=%4 | dropped pkts/s=%5 | queue max=%6/%7")
                                  .arg(datagramsThisSecond)
                                  .arg(avgFrameMs, 0, 'f', 3)
                                  .arg(avgInterpolationMs, 0, 'f', 3)
                                  .arg(recoveredLinesThisSecond)
                                  .arg(droppedPacketsThisSecond)
                                  .arg(maxQueuedPacketsThisSecond)
                                  .arg(kMaxQueuedPackets);
    emit performanceStatsChanged(statsText);

    frameCount = 0;
    datagramsThisSecond = 0;
    completedFramesThisSecond = 0;
    recoveredLinesThisSecond = 0;
    frameProcessingNsThisSecond = 0;
    interpolationNsThisSecond = 0;
    droppedPacketsThisSecond = 0;
    droppedBatchesThisSecond = 0;
    maxQueuedPacketsThisSecond = 0;
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
            frameBuffer.fill(QByteArray());
            receivedLineFlags.fill(false);
        }

        for (QList<QByteArray>::const_iterator it = batch.cbegin(); it != batch.cend(); ++it) {
            processFrameData(*it);
        }
    }
}

void UdpFrameProcessor::processFrameData(const QByteArray &data) {
    ++datagramsThisSecond;

    if (data.size() < kPacketHeaderSize) {
        qWarning() << "Incomplete packet received. Packet too small:" << data.size();
        return;
    }

    if (isMarkerPacket(data, char(0xAA))) {
        frameValid = true;
        currentLine = 0;
        frameBuffer.fill(QByteArray());
        receivedLineFlags.fill(false);
        return;
    }

    if (isMarkerPacket(data, char(0xBB))) {
        if (frameValid) {
            finalizeFrame();
        }
        frameValid = false;
        return;
    }

    if (!frameValid) {
        return;
    }

    if (currentLine < 0 || currentLine >= kFrameHeight) {
        qWarning() << "Invalid line number:" << currentLine;
        return;
    }

    const int payloadSize = data.size() - kPacketHeaderSize;
    QByteArray &lineBuffer = frameBuffer[currentLine];
    lineBuffer.resize(payloadSize);
    std::memcpy(lineBuffer.data(), data.constData() + kPacketHeaderSize, static_cast<size_t>(payloadSize));
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
        const QByteArray *topLine = (i > 0 && receivedLineFlags[i - 1]) ? &frameBuffer[i - 1] : nullptr;
        const QByteArray *bottomLine = (i + 1 < kFrameHeight && receivedLineFlags[i + 1]) ? &frameBuffer[i + 1] : nullptr;

        if (topLine != nullptr && bottomLine != nullptr) {
            QByteArray interpolatedLine(topLine->size(), 0);
            for (int j = 0; j < topLine->size(); ++j) {
                interpolatedLine[j] = static_cast<char>((static_cast<unsigned char>((*topLine)[j]) + static_cast<unsigned char>((*bottomLine)[j])) / 2);
            }
            frameBuffer[i] = interpolatedLine;
        } else if (topLine != nullptr) {
            frameBuffer[i] = *topLine;
        } else if (bottomLine != nullptr) {
            frameBuffer[i] = *bottomLine;
        }
    }

    interpolationNsThisSecond += static_cast<quint64>(interpolationTimer.nsecsElapsed());
    recoveredLinesThisSecond += recoveredLines;

    {
        QMutexLocker lock(&imageMutex);
        for (int i = 0; i < kFrameHeight; ++i) {
            const QByteArray &lineData = frameBuffer[i];
            if (lineData.isEmpty()) {
                continue;
            }

            uchar *imageBits = rawImage.bits() + (i * rawImage.bytesPerLine());
            const int pixelCount = lineData.size() / 2;
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
    update();

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

QImage UdpFrameProcessor::buildOutputFrame(const QImage &sourceFrame) const {
    if (sourceFrame.isNull()) {
        return QImage();
    }

    if (!flipHorizontal && !flipVertical) {
        return sourceFrame.copy();
    }

    return sourceFrame.mirrored(flipHorizontal, flipVertical);
}
