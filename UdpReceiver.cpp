/*
===================================================
Created on: 06-8-2024
Author: Chang Xu
File: UdpReceiver.cpp
Version: 4.4
Language: C++ (Qt Framework)
Description:
This file implements the UdpReceiver class,
which is responsible for receiving UDP packets,
managing the network interface using Tshark, and
emitting processed frame data to be used by
the frame processor. It includes functionalities
such as buffer clearing, real-time data capturing,
and packet processing.
===================================================
*/

#include "UdpReceiver.h"
#include <QDebug>

UdpReceiver::UdpReceiver(QObject *parent)
    : QObject(parent),
      mrecv(new QUdpSocket(this)),
      tsharkProcess(new QProcess(this)),
      bufferCleaner(new QTimer(this)) {
    // Periodically clear the buffer every 10 seconds
    connect(bufferCleaner, &QTimer::timeout, this, &UdpReceiver::clearBuffer);
    bufferCleaner->start(10000); // Clear buffer every 10 seconds
}

UdpReceiver::~UdpReceiver() {
    // Gracefully terminate the Tshark process
    if (tsharkProcess && tsharkProcess->state() == QProcess::Running) {
        tsharkProcess->terminate();
        if (!tsharkProcess->waitForFinished(3000)) {
            qWarning() << "Tshark did not exit gracefully, killing process.";
            tsharkProcess->kill();
            tsharkProcess->waitForFinished();
        }
    }
}

void UdpReceiver::startReceiving(const QString &address, quint16 port) {
    QHostAddress maddr(address);

    // Bind to the specified address and port
    if (!mrecv->bind(maddr, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "Failed to bind to address:" << address << "port:" << port;
        return;
    }

    qDebug() << "Listening for UDP packets on" << address << "port" << port;

    // Connect the signal to process incoming data
    connect(mrecv, &QUdpSocket::readyRead, this, &UdpReceiver::readPendingDatagrams);
}

void UdpReceiver::startTshark(const QString &interfaceName) {
    QString program = "D:/Program Files (x86)/Wireshark/tshark.exe";
    QString deviceName = interfaceName.isEmpty() ? "Ethernet 2" : interfaceName;

    // Set up circular buffer parameters
    QStringList arguments = {
        "-i", deviceName,          // Specify network interface to listen on
        "-l",                      // Enable real-time output
        "-n",                      // Disable hostname resolution
        "-b", "filesize:3145728",  // Maximum file size of 3GB (unit: KB)
        "-b", "files:3",           // Keep up to 3 files
        "-w", "E:/sharkfile/capture.pcap"  // Set capture file save path
    };

    qDebug() << "Starting tshark with program:" << program << "arguments:" << arguments;
    tsharkProcess->start(program, arguments);

    // Capture error output
    connect(tsharkProcess, &QProcess::readyReadStandardError, this, [=]() {
        QByteArray errorOutput = tsharkProcess->readAllStandardError();
        if (!errorOutput.isEmpty()) {
            qWarning() << "Tshark Error Output:" << errorOutput;
        }
    });

    // Check if Tshark started successfully
    if (!tsharkProcess->waitForStarted()) {
        qWarning() << "Failed to start tshark. Error:" << tsharkProcess->errorString();
    } else {
        qDebug() << "Tshark started successfully.";
    }
}

void UdpReceiver::readPendingDatagrams() {
    // Read incoming packets in bulk
    while (mrecv->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(mrecv->pendingDatagramSize());
        mrecv->readDatagram(datagram.data(), datagram.size());

        // Emit signal when a new frame is received
        if (!datagram.isEmpty()) {
            emit newFrameData(datagram);
        }
    }
}

#include <QtConcurrent>

void UdpReceiver::clearBuffer() {
    QtConcurrent::run([this]() {
        int packetsCleared = 0;

        while (mrecv->hasPendingDatagrams()) {
            mrecv->readDatagram(nullptr, 0); // Discard the packet
            packetsCleared++;
        }

        qDebug() << "Cleared" << packetsCleared << "datagrams from buffer.";
    });
}



