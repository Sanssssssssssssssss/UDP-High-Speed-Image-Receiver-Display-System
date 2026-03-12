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

void UdpFrameProcessor::invokeWorkerVoid(const char *method) {
    QMetaObject::invokeMethod(worker, method, Qt::QueuedConnection);
}

void UdpFrameProcessor::saveSnapshot(const QString &directory) {
    invokeWorkerUnary("saveSnapshot", directory);
}

void UdpFrameProcessor::toggleRecording(const QString &directory, const QString &format, int fps) {
    invokeWorkerTernary("toggleRecording", directory, format, fps);
}

void UdpFrameProcessor::setFlipHorizontal(bool enabled) {
    invokeWorkerUnary("setFlipHorizontal", enabled);
}

void UdpFrameProcessor::setFlipVertical(bool enabled) {
    invokeWorkerUnary("setFlipVertical", enabled);
}

void UdpFrameProcessor::setBrightness(int value) {
    invokeWorkerUnary("setBrightness", value);
}

void UdpFrameProcessor::setGamma(int value) {
    invokeWorkerUnary("setGamma", value);
}

void UdpFrameProcessor::setSharpness(int value) {
    invokeWorkerUnary("setSharpness", value);
}

void UdpFrameProcessor::setDenoise(int value) {
    invokeWorkerUnary("setDenoise", value);
}

void UdpFrameProcessor::applyReceiverSettings(const QString &address, quint16 port) {
    invokeWorkerBinary("applyReceiverSettings", address, port);
}

void UdpFrameProcessor::setAiDetectionEnabled(bool enabled) {
    invokeWorkerUnary("setAiDetectionEnabled", enabled);
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
