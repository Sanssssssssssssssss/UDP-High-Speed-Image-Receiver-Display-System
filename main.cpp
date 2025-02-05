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
record video. The application relies on Qt’s signal-slot
mechanism for seamless interaction between components.
===================================================
*/

#include <QApplication>
#include <QHBoxLayout>
#include <QWidget>
#include "UdpFrameProcessor.h"
#include "ControlUI.h"
#include "UdpReceiver.h"
#include <QCoreApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    UdpReceiver receiver;
    //receiver.startTshark("以太网"); // Start Tshark
    receiver.startTshark("Ethernet"); // Start Tshark
    // receiver.startReceiving("192.168.1.102", 8080); // Receive UDP data

    // Create main container widget
    QWidget mainWidget;
    mainWidget.setWindowTitle("UDP Frame Simulation with Control Panel");
    mainWidget.resize(1120, 800);

    // Create layout for main widget
    QHBoxLayout *mainLayout = new QHBoxLayout(&mainWidget);
    mainLayout->setSpacing(0);  // Set spacing to 0 to prevent the layout from expanding

    // Set up UdpFrameProcessor (left side)
    UdpFrameProcessor *videoDisplay = new UdpFrameProcessor(&mainWidget);
    mainLayout->addWidget(videoDisplay, 1);

    // Set up ControlUI (right side)
    ControlUI *controlUI = new ControlUI(&mainWidget);
    mainLayout->addWidget(controlUI);

    // Connect FPS signal to ControlUI
    QObject::connect(videoDisplay, &UdpFrameProcessor::fpsChanged, controlUI, &ControlUI::onFPSChanged);

    // Connect snapshotRequested signal to UdpFrameProcessor
    QObject::connect(controlUI, &ControlUI::snapshotRequested, videoDisplay, &UdpFrameProcessor::saveSnapshot, Qt::QueuedConnection);

    // Connect recordingRequested signal to UdpFrameProcessor
    QObject::connect(controlUI, &ControlUI::recordingRequested,
                     videoDisplay, [=](const QString &directory, const QString &format) {
        videoDisplay->toggleRecording(directory, format, 30);  // Only FPS parameters are passed, width and height are automatically obtained from the image resolution
    });

    // Connect flipHorizontalRequested and flipVerticalRequested signals
    QObject::connect(controlUI, &ControlUI::flipHorizontalRequested, videoDisplay, &UdpFrameProcessor::setFlipHorizontal, Qt::QueuedConnection);
    QObject::connect(controlUI, &ControlUI::flipVerticalRequested, videoDisplay, &UdpFrameProcessor::setFlipVertical, Qt::QueuedConnection);

    QObject::connect(videoDisplay, &UdpFrameProcessor::recordingStateChanged, controlUI, &ControlUI::onRecordingStateChanged);

    // Set the layout for the main widget
    mainWidget.setLayout(mainLayout);
    mainWidget.show();

    return app.exec();
}

