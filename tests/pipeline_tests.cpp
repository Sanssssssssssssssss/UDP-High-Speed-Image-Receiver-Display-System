#include "UdpFramePipelineWorker.h"
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <opencv2/videoio.hpp>

class PipelineTests : public QObject {
    Q_OBJECT
    static QByteArray marker(char value) { return QByteArray(4, 0) + QByteArray(800, value); }
    static QByteArray redLine() {
        QByteArray packet(804, 0);
        for (int x = 4; x < packet.size(); x += 2) packet[x] = char(0xf8);
        return packet;
    }
private slots:
    void reconstructAndSnapshot() {
        UdpFramePipelineWorker worker;
        QSignalSpy frames(&worker, &UdpFramePipelineWorker::frameReady);
        worker.processFrameData(marker(char(0xbb))); // orphan end cannot publish
        worker.processFrameData(redLine());
        QCOMPARE(frames.count(), 0);
        worker.processFrameData(marker(char(0xaa)));
        for (int row = 0; row < 400; ++row) worker.processFrameData(redLine());
        worker.processFrameData(marker(char(0xbb)));
        QCOMPARE(frames.count(), 1);
        const QImage frame = qvariant_cast<QImage>(frames.takeFirst().at(0));
        QCOMPARE(frame.size(), QSize(400, 400));
        QCOMPARE(frame.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(frame.pixelColor(399, 399), QColor(255, 0, 0));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        worker.saveSnapshot(dir.path());
        const QStringList files = QDir(dir.path()).entryList({"*.png"});
        QCOMPARE(files.size(), 1);
        QCOMPARE(QImage(dir.filePath(files.first())).convertToFormat(frame.format()), frame);
    }
    void shortFrameRecoveryAndBounds() {
        UdpFramePipelineWorker worker;
        worker.processFrameData(QByteArray(3, 0));
        worker.processFrameData(marker(char(0xaa)));
        worker.processFrameData(redLine());
        worker.processFrameData(marker(char(0xbb)));
        QCOMPARE(worker.rawImage.pixelColor(399, 399), QColor(255, 0, 0));
        QCOMPARE(worker.recoveredLinesThisSecond, quint64(399));
        worker.processFrameData(marker(char(0xaa)));
        worker.processFrameData(QByteArray(4, 0) + QByteArray::fromHex("07e0"));
        worker.processFrameData(marker(char(0xbb)));
        QCOMPARE(worker.rawImage.pixelColor(0, 0), QColor(0, 255, 0));
        QCOMPARE(worker.rawImage.pixelColor(1, 0), QColor(0, 0, 0));
        worker.processFrameData(marker(char(0xaa)));
        for (int row = 0; row < 402; ++row) worker.processFrameData(redLine());
        QCOMPARE(worker.currentLine, 400);
        QCOMPARE(worker.overflowLinePacketsThisSecond, quint64(2));
    }
    void queueOverflowResynchronizes() {
        UdpFramePipelineWorker worker;
        worker.processFrameData(marker(char(0xaa)));
        QList<QByteArray> batch;
        for (int i = 0; i < 1024; ++i) batch.append(redLine());
        for (int i = 0; i < 4; ++i) worker.enqueueFrameBatch(batch);
        QVERIFY(worker.pendingPacketCount <= worker.kMaxQueuedPackets);
        QVERIFY(worker.parserResyncPending);
        worker.drainPendingBatches();
        QVERIFY(!worker.frameValid);
        QCOMPARE(worker.parserResyncEventsThisSecond, quint64(1));
    }
    void udpLoopback() {
        QUdpSocket reservation;
        QVERIFY(reservation.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        const quint16 port = reservation.localPort();
        reservation.close();
        UdpReceiver receiver;
        QSignalSpy binding(&receiver, &UdpReceiver::receiverBindingChanged);
        QSignalSpy batches(&receiver, &UdpReceiver::newFrameBatch);
        receiver.startReceiving("127.0.0.1", port, false, "");
        QCOMPARE(binding.count(), 1);
        QVERIFY(binding.first().at(2).toBool());
        QUdpSocket sender;
        const QByteArray packet = redLine();
        QCOMPARE(sender.writeDatagram(packet, QHostAddress::LocalHost, port), qint64(packet.size()));
        QTRY_VERIFY_WITH_TIMEOUT(!batches.isEmpty(), 3000);
        QCOMPARE(qvariant_cast<QList<QByteArray>>(batches.first().first()).first(), packet);
        receiver.stopReceiving();
    }
    void recordingCanBeDecoded() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        VideoRecorderWorker recorder;
        QSignalSpy states(&recorder, &VideoRecorderWorker::recordingStateChanged);
        recorder.startRecording(dir.path(), "avi", 30, QSize(400, 400));
        QVERIFY(!states.isEmpty() && states.last().first().toBool());
        QImage frame(400, 400, QImage::Format_RGB888);
        frame.fill(Qt::red);
        recorder.enqueueFrame(frame);
        QVERIFY(QMetaObject::invokeMethod(&recorder, "processQueue", Qt::DirectConnection));
        recorder.stopRecording();
        const QStringList files = QDir(dir.path()).entryList({"*.avi"});
        QCOMPARE(files.size(), 1);
        cv::VideoCapture capture(dir.filePath(files.first()).toStdString());
        cv::Mat decoded;
        QVERIFY(capture.read(decoded));
        QCOMPARE(decoded.cols, 400);
        QCOMPARE(decoded.rows, 400);
        QVERIFY(decoded.at<cv::Vec3b>(200, 200)[2] > 240);
    }
};
QTEST_GUILESS_MAIN(PipelineTests)
#include "pipeline_tests.moc"
