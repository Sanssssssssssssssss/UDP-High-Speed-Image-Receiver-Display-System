/*
===================================================
Created on: 06-8-2024
Author: Chang Xu
File: main.cpp
Version: 3.12
Language: C++ (Qt Framework)
Description:
This file implements the main entry point for the UDP-based
image processing application. It initializes the UI components,
sets up the UDP frame receiver, and connects various UI elements
to the image processing backend. The program creates a graphical
interface that allows users to visualize incoming video data,
adjust image processing parameters, capture snapshots, and
record video.
===================================================
*/

#include "ApplicationLauncher.h"
#include "ControlUI.h"
#include "UdpFrameProcessor.h"
#include "UdpReceiver.h"
#include <QApplication>
#include <QByteArray>
#include <QFrame>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QFont>
#include <QSizePolicy>
#include <QElapsedTimer>
#include <QTimer>
#include <QUdpSocket>
#include <QVBoxLayout>
#include <QWidget>

namespace {
const qint64 kDemoFrameIntervalNs = 1000000000LL / 60LL;

class LocalDemoSender : public QObject {
public:
    explicit LocalDemoSender(QObject *parent = nullptr)
        : QObject(parent),
          socket(new QUdpSocket(this)),
          timer(new QTimer(this)),
          frameIndex(0),
          nextFrameDeadlineNs(0) {
        timer->setTimerType(Qt::PreciseTimer);
        connect(timer, &QTimer::timeout, this, [this]() { sendDueFrames(); });
        cadenceTimer.start();
        timer->start(1);
    }

private:
    static quint16 packRgb565(int r, int g, int b) {
        const quint16 r5 = static_cast<quint16>((r >> 3) & 0x1F);
        const quint16 g6 = static_cast<quint16>((g >> 2) & 0x3F);
        const quint16 b5 = static_cast<quint16>((b >> 3) & 0x1F);
        return static_cast<quint16>((r5 << 11) | (g6 << 5) | b5);
    }

    QByteArray makeMarkerPacket(char marker) const {
        QByteArray datagram(4 + (400 * 2), marker);
        datagram[0] = 0x55;
        datagram[1] = 0x33;
        datagram[2] = 0x11;
        datagram[3] = 0x77;
        return datagram;
    }

    QByteArray makeLinePacket(int line) const {
        QByteArray datagram(4 + (400 * 2), 0);
        datagram[0] = static_cast<char>((frameIndex >> 8) & 0xFF);
        datagram[1] = static_cast<char>(frameIndex & 0xFF);
        datagram[2] = static_cast<char>((line >> 8) & 0xFF);
        datagram[3] = static_cast<char>(line & 0xFF);

        for (int x = 0; x < 400; ++x) {
            const int movingBar = (x + frameIndex * 4) % 400;
            const int r = (x + frameIndex * 3) & 0xFF;
            const int g = (line * 2 + frameIndex * 5) & 0xFF;
            const int b = ((movingBar ^ line) * 3) & 0xFF;
            const quint16 rgb565 = packRgb565(r, g, b);
            datagram[4 + (x * 2)] = static_cast<char>((rgb565 >> 8) & 0xFF);
            datagram[4 + (x * 2) + 1] = static_cast<char>(rgb565 & 0xFF);
        }

        return datagram;
    }

    void sendFrame() {
        socket->writeDatagram(makeMarkerPacket(char(0xAA)), QHostAddress::LocalHost, 8080);

        const int droppedLine = (frameIndex % 45 == 0) ? ((frameIndex / 45) % 400) : -1;
        for (int line = 0; line < 400; ++line) {
            if (line == droppedLine) {
                continue;
            }
            socket->writeDatagram(makeLinePacket(line), QHostAddress::LocalHost, 8080);
        }

        socket->writeDatagram(makeMarkerPacket(char(0xBB)), QHostAddress::LocalHost, 8080);
        ++frameIndex;
    }

    void sendDueFrames() {
        const qint64 nowNs = cadenceTimer.nsecsElapsed();
        if (nextFrameDeadlineNs == 0) {
            nextFrameDeadlineNs = nowNs;
        }

        while (nowNs >= nextFrameDeadlineNs) {
            sendFrame();
            nextFrameDeadlineNs += kDemoFrameIntervalNs;
        }
    }

    QUdpSocket *socket;
    QTimer *timer;
    int frameIndex;
    QElapsedTimer cadenceTimer;
    qint64 nextFrameDeadlineNs;
};
}

int runApplication(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setFont(QFont("Segoe UI", 11));
    const QStringList args = app.arguments();
    const bool enableDemo = args.contains("--demo") || qEnvironmentVariableIntValue("POST_TRAIN_DEMO") == 1;
    const bool enableCaptureBootstrap = args.contains("--capture-bootstrap") || qEnvironmentVariableIntValue("POST_TRAIN_CAPTURE") == 1;

    UdpReceiver captureBootstrap;
    if (enableCaptureBootstrap) {
        captureBootstrap.startTshark("Ethernet");
    }

    QWidget mainWidget;
    mainWidget.setObjectName("MainWindow");
    mainWidget.setWindowTitle("Post-Train UDP Vision Console");
    mainWidget.resize(1760, 1080);
    mainWidget.setStyleSheet(R"(
        QWidget#MainWindow {
            background: #09090a;
        }
        QFrame#VideoPanel {
            background: #111113;
            border: 1px solid #222226;
            border-radius: 30px;
        }
        QLabel#VideoEyebrow {
            color: #8e8e94;
            font-size: 12px;
            font-weight: 700;
            letter-spacing: 1px;
        }
        QLabel#VideoTitle {
            color: #fafafa;
            font-size: 34px;
            font-weight: 700;
        }
        QLabel#VideoSubtitle {
            color: #a7a7ad;
            font-size: 14px;
        }
        QLabel#ModeBadge {
            background: #1a1a1d;
            color: #f1f1f3;
            border: 1px solid #2d2d31;
            border-radius: 14px;
            padding: 6px 12px;
            font-size: 12px;
            font-weight: 700;
        }
        QWidget#VideoSurface {
            background: #020202;
            border: 1px solid #2a2a2e;
            border-radius: 24px;
        }
    )");

    QHBoxLayout *mainLayout = new QHBoxLayout(&mainWidget);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    auto *videoPanel = new QFrame(&mainWidget);
    videoPanel->setObjectName("VideoPanel");
    auto *videoPanelLayout = new QVBoxLayout(videoPanel);
    videoPanelLayout->setContentsMargins(28, 26, 28, 28);
    videoPanelLayout->setSpacing(20);

    auto *videoHeaderLayout = new QHBoxLayout();
    videoHeaderLayout->setContentsMargins(0, 0, 0, 0);
    videoHeaderLayout->setSpacing(12);

    auto *videoHeaderTextLayout = new QVBoxLayout();
    videoHeaderTextLayout->setContentsMargins(0, 0, 0, 0);
    videoHeaderTextLayout->setSpacing(4);

    auto *videoEyebrow = new QLabel("LIVE VIDEO SURFACE", videoPanel);
    videoEyebrow->setObjectName("VideoEyebrow");
    videoHeaderTextLayout->addWidget(videoEyebrow);

    auto *videoTitle = new QLabel("Realtime UDP Receiver", videoPanel);
    videoTitle->setObjectName("VideoTitle");
    videoHeaderTextLayout->addWidget(videoTitle);

    const QString subtitleText = enableDemo
        ? "Local demo traffic is active at 60 fps and about 24k UDP packets per second for stress testing."
        : "Hardware input mode is active. Start with --demo or POST_TRAIN_DEMO=1 when you want the built-in UDP stress source.";
    auto *videoSubtitle = new QLabel(subtitleText, videoPanel);
    videoSubtitle->setObjectName("VideoSubtitle");
    videoSubtitle->setWordWrap(true);
    videoHeaderTextLayout->addWidget(videoSubtitle);

    videoHeaderLayout->addLayout(videoHeaderTextLayout, 1);

    auto *modeBadge = new QLabel(enableDemo ? "LOOPBACK DEMO" : "HARDWARE INPUT", videoPanel);
    modeBadge->setObjectName("ModeBadge");
    modeBadge->setAlignment(Qt::AlignCenter);
    videoHeaderLayout->addWidget(modeBadge, 0, Qt::AlignTop);

    videoPanelLayout->addLayout(videoHeaderLayout);

    UdpFrameProcessor *videoDisplay = new UdpFrameProcessor(videoPanel);
    videoDisplay->setObjectName("VideoSurface");
    videoDisplay->setAttribute(Qt::WA_StyledBackground, true);
    videoDisplay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoPanelLayout->addWidget(videoDisplay, 1);
    mainLayout->addWidget(videoPanel, 1);

    ControlUI *controlUI = new ControlUI(&mainWidget);
    controlUI->setFixedWidth(450);
    mainLayout->addWidget(controlUI);

    if (enableDemo) {
        LocalDemoSender *demoSender = new LocalDemoSender(&mainWidget);
        Q_UNUSED(demoSender);
    }

    QObject::connect(videoDisplay, &UdpFrameProcessor::fpsChanged, controlUI, &ControlUI::onFPSChanged);
    QObject::connect(videoDisplay, &UdpFrameProcessor::performanceStatsChanged, controlUI, &ControlUI::onPerformanceStatsChanged);
    QObject::connect(controlUI, &ControlUI::snapshotRequested, videoDisplay, &UdpFrameProcessor::saveSnapshot, Qt::QueuedConnection);
    QObject::connect(controlUI, &ControlUI::brightnessChanged, videoDisplay, &UdpFrameProcessor::setBrightness, Qt::QueuedConnection);
    QObject::connect(controlUI, &ControlUI::gammaChanged, videoDisplay, &UdpFrameProcessor::setGamma, Qt::QueuedConnection);
    QObject::connect(controlUI, &ControlUI::sharpnessChanged, videoDisplay, &UdpFrameProcessor::setSharpness, Qt::QueuedConnection);
    QObject::connect(controlUI, &ControlUI::denoiseChanged, videoDisplay, &UdpFrameProcessor::setDenoise, Qt::QueuedConnection);
    QObject::connect(controlUI, &ControlUI::recordingRequested, videoDisplay,
                     [=](const QString &directory, const QString &format) {
                         videoDisplay->toggleRecording(directory, format, 30);
                     });
    QObject::connect(controlUI, &ControlUI::flipHorizontalRequested, videoDisplay, &UdpFrameProcessor::setFlipHorizontal, Qt::QueuedConnection);
    QObject::connect(controlUI, &ControlUI::flipVerticalRequested, videoDisplay, &UdpFrameProcessor::setFlipVertical, Qt::QueuedConnection);
    QObject::connect(videoDisplay, &UdpFrameProcessor::recordingStateChanged, controlUI, &ControlUI::onRecordingStateChanged);

    mainWidget.setLayout(mainLayout);
    mainWidget.show();

    return app.exec();
}
