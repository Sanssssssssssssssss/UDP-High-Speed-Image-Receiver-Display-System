#include "UdpFrameProcessor.h"
#include "UdpFramePipelineWorker.h"
#include <QMetaObject>
#include <QPainter>

UdpFrameProcessor::UdpFrameProcessor(QWidget *parent)
    : QWidget(parent),
      workerThread(new QThread(this)),
      worker(new UdpFramePipelineWorker()),
      presentTimer(new QTimer(this)),
      framePendingPresentation(false),
      presentedFrameCount(0) {
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<QSize>("QSize");

    setMinimumSize(960, 960);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    worker->moveToThread(workerThread);
    connect(workerThread, &QThread::started, worker, &UdpFramePipelineWorker::start);
    connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);

    connect(this, &UdpFrameProcessor::saveSnapshotRequested, worker, &UdpFramePipelineWorker::saveSnapshot, Qt::QueuedConnection);
    connect(this, &UdpFrameProcessor::toggleRecordingRequested, worker, &UdpFramePipelineWorker::toggleRecording, Qt::QueuedConnection);
    connect(this, &UdpFrameProcessor::flipHorizontalRequested, worker, &UdpFramePipelineWorker::setFlipHorizontal, Qt::QueuedConnection);
    connect(this, &UdpFrameProcessor::flipVerticalRequested, worker, &UdpFramePipelineWorker::setFlipVertical, Qt::QueuedConnection);
    connect(this, &UdpFrameProcessor::brightnessRequested, worker, &UdpFramePipelineWorker::setBrightness, Qt::QueuedConnection);
    connect(this, &UdpFrameProcessor::gammaRequested, worker, &UdpFramePipelineWorker::setGamma, Qt::QueuedConnection);
    connect(this, &UdpFrameProcessor::sharpnessRequested, worker, &UdpFramePipelineWorker::setSharpness, Qt::QueuedConnection);
    connect(this, &UdpFrameProcessor::denoiseRequested, worker, &UdpFramePipelineWorker::setDenoise, Qt::QueuedConnection);
    connect(this, &UdpFrameProcessor::receiverSettingsApplyRequested, worker, &UdpFramePipelineWorker::applyReceiverSettings, Qt::QueuedConnection);
    connect(this, &UdpFrameProcessor::aiDetectionRequested, worker, &UdpFramePipelineWorker::setAiDetectionEnabled, Qt::QueuedConnection);

    connect(worker, &UdpFramePipelineWorker::frameReady, this, &UdpFrameProcessor::onWorkerFrameReady, Qt::QueuedConnection);
    connect(worker, &UdpFramePipelineWorker::statsReady, this, &UdpFrameProcessor::onWorkerStatsReady, Qt::QueuedConnection);
    connect(worker, &UdpFramePipelineWorker::recordingStateChanged, this, &UdpFrameProcessor::recordingStateChanged, Qt::QueuedConnection);
    connect(worker, &UdpFramePipelineWorker::receiverStatusChanged, this, &UdpFrameProcessor::receiverStatusChanged, Qt::QueuedConnection);
    connect(worker, &UdpFramePipelineWorker::receiverSettingsChanged, this, &UdpFrameProcessor::receiverSettingsChanged, Qt::QueuedConnection);
    connect(worker, &UdpFramePipelineWorker::aiStatusChanged, this, &UdpFrameProcessor::aiStatusChanged, Qt::QueuedConnection);

    presentTimer->setTimerType(Qt::PreciseTimer);
    connect(presentTimer, &QTimer::timeout, this, &UdpFrameProcessor::presentLatestFrame);
    presentTimer->start(16);

    workerThread->start();
}

UdpFrameProcessor::~UdpFrameProcessor() {
    QMetaObject::invokeMethod(worker, "shutdown", Qt::BlockingQueuedConnection);
    workerThread->quit();
    workerThread->wait();
}

void UdpFrameProcessor::saveSnapshot(const QString &directory) {
    emit saveSnapshotRequested(directory);
}

void UdpFrameProcessor::toggleRecording(const QString &directory, const QString &format, int fps) {
    emit toggleRecordingRequested(directory, format, fps);
}

void UdpFrameProcessor::setFlipHorizontal(bool enabled) {
    emit flipHorizontalRequested(enabled);
}

void UdpFrameProcessor::setFlipVertical(bool enabled) {
    emit flipVerticalRequested(enabled);
}

void UdpFrameProcessor::setBrightness(int value) {
    emit brightnessRequested(value);
}

void UdpFrameProcessor::setGamma(int value) {
    emit gammaRequested(value);
}

void UdpFrameProcessor::setSharpness(int value) {
    emit sharpnessRequested(value);
}

void UdpFrameProcessor::setDenoise(int value) {
    emit denoiseRequested(value);
}

void UdpFrameProcessor::applyReceiverSettings(const QString &address, quint16 port) {
    emit receiverSettingsApplyRequested(address, port);
}

void UdpFrameProcessor::setAiDetectionEnabled(bool enabled) {
    emit aiDetectionRequested(enabled);
}

void UdpFrameProcessor::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QImage frameToDraw;
    {
        QMutexLocker lock(&frameMutex);
        frameToDraw = presentedFrame;
    }

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (frameToDraw.isNull()) {
        return;
    }

    const QSize drawSize = frameToDraw.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect targetRect(QPoint((width() - drawSize.width()) / 2, (height() - drawSize.height()) / 2), drawSize);
    painter.drawImage(targetRect, frameToDraw);
}

void UdpFrameProcessor::presentLatestFrame() {
    if (!framePendingPresentation) {
        return;
    }

    {
        QMutexLocker lock(&frameMutex);
        presentedFrame = pendingFrame;
        framePendingPresentation = false;
    }

    ++presentedFrameCount;
    update();
}

void UdpFrameProcessor::onWorkerFrameReady(const QImage &frame) {
    QMutexLocker lock(&frameMutex);
    pendingFrame = frame;
    framePendingPresentation = true;
}

void UdpFrameProcessor::onWorkerStatsReady(const QString &statsText) {
    emit fpsChanged(presentedFrameCount);
    emit performanceStatsChanged(QString("%1 | present fps=%2").arg(statsText).arg(presentedFrameCount));
    presentedFrameCount = 0;
    latestWorkerStats = statsText;
}
