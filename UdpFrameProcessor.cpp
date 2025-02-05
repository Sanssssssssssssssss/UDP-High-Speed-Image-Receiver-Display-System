/*
===================================================
Created on: 06-8-2024
Author: Chang Xu
File: mainwindow.cpp
Version: 1.5
Language: C++ (Qt Framework)
Description:
This file defines the main window of the UDP-based
image processing application. It initializes the user
interface and manages the lifecycle of the UI components.
The MainWindow class serves as the central container
for the application, handling initialization and cleanup
of UI elements.
===================================================
*/


#include "UdpFrameProcessor.h"

UdpFrameProcessor::UdpFrameProcessor(QWidget *parent)
    : QWidget(parent), frameCount(0), receivedLines(0), flipHorizontal(false), flipVertical(false), isRecording(false) {
    // Initialize the image and set a black background
    image = QImage(400, 400, QImage::Format_RGB888);
    image.fill(Qt::black);

    // Set up the FPS timer
    fpsTimer = new QTimer(this);
    connect(fpsTimer, &QTimer::timeout, this, &UdpFrameProcessor::updateFPS);
    fpsTimer->start(1000);  // Update FPS every second

    qDebug() << "UdpFrameProcessor initialized";

    // Set up UDP receiver and move to a new thread
    receiver = new UdpReceiver();
    receiverThread = new QThread();
    receiver->moveToThread(receiverThread);
    connect(receiverThread, &QThread::started, receiver, [=]() { receiver->startReceiving("192.168.1.102", 8080); });
    connect(receiver, &UdpReceiver::newFrameData, this, &UdpFrameProcessor::processFrameData, Qt::QueuedConnection);
    connect(receiverThread, &QThread::finished, receiver, &QObject::deleteLater);
    connect(receiverThread, &QThread::finished, receiverThread, &QObject::deleteLater);
    receiverThread->start();
}

UdpFrameProcessor::~UdpFrameProcessor() {
    receiverThread->quit();
    receiverThread->wait();  // Wait for the thread to finish
    delete receiver;

    if (videoWriter.isOpened()) {
        videoWriter.release();
    }
}

void UdpFrameProcessor::writeFrameToVideo() {
    QImage rgbFrame = image.convertToFormat(QImage::Format_RGB888);
    cv::Mat mat(rgbFrame.height(), rgbFrame.width(), CV_8UC3, const_cast<uchar *>(rgbFrame.bits()), rgbFrame.bytesPerLine());
    cv::Mat matBGR;
    cv::cvtColor(mat, matBGR, cv::COLOR_RGB2BGR);

    if (!matBGR.empty()) {
        videoWriter.write(matBGR);
        qDebug() << "Frame written to video.";
    } else {
        qWarning() << "Frame data is empty or invalid. Skipping frame.";
    }
}

void UdpFrameProcessor::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);

    // Enable anti-aliasing
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Set up scaling and drawing rectangles
    QRect targetRect(0, 0, 800, 800);  // Target area is 800x800
    QRect sourceRect(0, 0, image.width(), image.height());  // Original image size

    // Apply transformations for flipping
    QTransform transform;
    if (flipHorizontal) transform.scale(-1, 1);  // Horizontal flip
    if (flipVertical) transform.scale(1, -1);    // Vertical flip

    painter.setTransform(transform);

    // Draw the image
    painter.drawImage(targetRect, image, sourceRect);
}

void UdpFrameProcessor::updateFPS() {
    emit fpsChanged(frameCount);
    frameCount = 0;  // Reset frame counter
}

void UdpFrameProcessor::processFrameData(const QByteArray &data) {
    QMetaObject::invokeMethod(this, [=]() {
        static bool frameValid = false;                  // Whether or not valid frames are being constructed
        static int currentLine = 0;                      // The line number currently being processed
        static QVector<QByteArray> frameBuffer(400);     // Temporarily caches 400 lines of image data
        static QVector<bool> receivedLines(400, false);  // Marks each line as received

        if (data.size() < 4) {                           // Check minimum packet size
            qWarning() << "Incomplete packet received. Packet too small:" << data.size();
            return;
        }

        QMutexLocker lock(&imageMutex);                  // Protecting Image Access

        // Check frame header packet (ignore first 4 bytes, all subsequent 0xAA)
        if (data.mid(4).trimmed() == QByteArray(data.size() - 4, char(0xAA))) {
            // qDebug() << "Frame start packet received.";
            frameValid = true;
            currentLine = 0;
            frameBuffer.fill(QByteArray());              // Flush the frame buffer
            receivedLines.fill(false);                   // Reset line receive state
            return;
        }

        // Check end-of-frame packet (ignore first 4 bytes, all subsequent are 0xBB)
        if (data.mid(4).trimmed() == QByteArray(data.size() - 4, char(0xBB))) {
            // qDebug() << "Frame end packet received.";
            if (frameValid) {
                // Interpolation compensation for missing rows
                for (int i = 0; i < 400; ++i) {
                    if (!receivedLines[i]) {
                        // up-down interpolation
                        QByteArray topLine = (i > 0 && receivedLines[i - 1]) ? frameBuffer[i - 1] : QByteArray();
                        QByteArray bottomLine = (i < 399 && receivedLines[i + 1]) ? frameBuffer[i + 1] : QByteArray();

                        if (!topLine.isEmpty() && !bottomLine.isEmpty()) {
                            QByteArray interpolatedLine(topLine.size(), 0);
                            for (int j = 0; j < topLine.size(); ++j) {
                                interpolatedLine[j] = (topLine[j] + bottomLine[j]) / 2;
                            }
                            frameBuffer[i] = interpolatedLine;
                        } else if (!topLine.isEmpty()) {
                            frameBuffer[i] = topLine;     // Fill with the previous line
                        } else if (!bottomLine.isEmpty()) {
                            frameBuffer[i] = bottomLine;  // Fill in with the next line
                        }
                    }
                }

                // Copy data to image and refresh
                for (int i = 0; i < 400; ++i) {
                    if (!frameBuffer[i].isEmpty()) {
                        uchar *imageBits = image.bits() + (i * image.bytesPerLine());
                        const QByteArray &lineData = frameBuffer[i];
                        for (int j = 0; j < lineData.size() / 2; ++j) {
                            quint16 rgb565 = static_cast<quint16>((lineData[j * 2] << 8) | (lineData[j * 2 + 1] & 0xFF));

                            // RGB565 -> RGB888 conversion correction
                            uchar r = (rgb565 >> 11) & 0x1F;
                            uchar g = (rgb565 >> 5) & 0x3F;
                            uchar b = rgb565 & 0x1F;

                            r = (r << 3) | (r >> 2);
                            g = (g << 2) | (g >> 4);
                            b = (b << 3) | (b >> 2);

                            imageBits[j * 3] = r;
                            imageBits[j * 3 + 1] = g;
                            imageBits[j * 3 + 2] = b;
                        }
                    }
                }

                frameCount++;
                update();  // trigger refresh
                if (isRecording && videoWriter.isOpened()) {
                    writeFrameToVideo();  // Save current frame to video
                }

                // qDebug() << "Frame completed and updated.";
            }
            frameValid = false; // End current frame
            return;
        }

        // Processing common line packets
        if (frameValid) {
            int index = currentLine; // Use the current line number as the index
            if (index >= 0 && index < 400) {
                frameBuffer[index] = data.mid(4);      // Stores RGB565 data, ignores first 4 bytes
                receivedLines[index] = true;           // Marks the line as received
                // qDebug() << "Line" << currentLine << "updated.";
                currentLine++;
            } else {
                qWarning() << "Invalid line number:" << currentLine;
            }
        }
        }, Qt::QueuedConnection);
}


QImage UdpFrameProcessor::getCurrentFrame() {
    QMutexLocker lock(&imageMutex);
    return image.copy();  // Return a copy of the image to ensure thread safety
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
        // Make sure the catalog exists
        QDir dir(directory);
        if (!dir.exists()) {
            qWarning() << "Directory does not exist. Attempting to create:" << directory;
            if (!dir.mkpath(".")) {
                qWarning() << "Failed to create directory:" << directory;
                emit recordingStateChanged(false);
                return;
            }
        }

        QString fileName = directory + "/recording_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "." + format;
        int codec = (format == "avi") ? cv::VideoWriter::fourcc('M', 'J', 'P', 'G') : cv::VideoWriter::fourcc('H', '2', '6', '4');
        int frameWidth = image.width();
        int frameHeight = image.height();

        qDebug() << "Attempting to open file:" << fileName;
        qDebug() << "Codec:" << codec;
        qDebug() << "Resolution:" << frameWidth << "x" << frameHeight;
        qDebug() << "FPS:" << fps;

        try {
            // Try to open the video writer
            videoWriter.open(fileName.toStdString(), codec, fps, cv::Size(frameWidth, frameHeight));
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

        // Start recording
        isRecording = true;
        emit recordingStateChanged(true);  // Notification UI updates recording status
        qDebug() << "Recording started.";
    } else {
        // Stop Recording Logic
        if (videoWriter.isOpened()) {
            videoWriter.release();
            qDebug() << "VideoWriter released, recording stopped.";
        } else {
            qWarning() << "VideoWriter was not open but recording stop requested.";
        }

        // Stop Recording Status
        isRecording = false;
        emit recordingStateChanged(false);  // Notification UI updates recording status
        qDebug() << "Recording stopped.";
    }
}

void UdpFrameProcessor::setFlipHorizontal(bool enabled) {
    flipHorizontal = enabled;
    // update();  // Request a repaint to reflect the change
}

void UdpFrameProcessor::setFlipVertical(bool enabled) {
    flipVertical = enabled;
    // update();  // Request a repaint to reflect the change
}
