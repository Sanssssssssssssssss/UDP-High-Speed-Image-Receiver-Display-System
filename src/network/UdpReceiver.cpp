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
the frame processor.
===================================================
*/

#include "UdpReceiver.h"
#include <QAbstractSocket>
#include <QDebug>
#include <QFileInfo>

namespace {
const int kDesiredReceiveBufferBytes = 16 * 1024 * 1024;
const int kMaxBatchPackets = 1024;
}

UdpReceiver::UdpReceiver(QObject *parent)
    : QObject(parent),
      mrecv(new QUdpSocket(this)),
      tsharkProcess(new QProcess(this)),
      boundPort(0) {
    qRegisterMetaType<QList<QByteArray> >("QList<QByteArray>");
    qRegisterMetaType<quint16>("quint16");

    connect(mrecv, &QUdpSocket::readyRead, this, &UdpReceiver::readPendingDatagrams);
    connect(mrecv, &QUdpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError socketError) {
        Q_UNUSED(socketError);
        qWarning() << "UDP socket error:" << mrecv->errorString();
    });
}

UdpReceiver::~UdpReceiver() {
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

    if (maddr.isNull()) {
        const QString message = QString("Bind failed: invalid address %1").arg(address);
        qWarning() << message;
        emit receiverBindingChanged(address, port, false, message);
        return;
    }

    stopReceiving();

    if (!mrecv->bind(maddr, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        const QString message = QString("Bind failed on %1:%2 (%3)")
                                    .arg(address)
                                    .arg(port)
                                    .arg(mrecv->errorString());
        qWarning() << message;
        emit receiverBindingChanged(address, port, false, message);
        return;
    }

    boundAddress = address;
    boundPort = port;
    mrecv->setReadBufferSize(kDesiredReceiveBufferBytes);
    mrecv->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, kDesiredReceiveBufferBytes);
    const int actualBuffer = mrecv->socketOption(QAbstractSocket::ReceiveBufferSizeSocketOption).toInt();
    const QString message = QString("Listening on %1:%2 | recv buffer=%3 bytes")
                                .arg(address)
                                .arg(port)
                                .arg(actualBuffer);
    qDebug() << message;
    emit receiverBindingChanged(address, port, true, message);
}

void UdpReceiver::stopReceiving() {
    if (!mrecv->isOpen()) {
        return;
    }

    mrecv->close();
}

void UdpReceiver::startTshark(const QString &interfaceName) {
    QString program = "D:/Program Files (x86)/Wireshark/tshark.exe";
    QString deviceName = interfaceName.isEmpty() ? "Ethernet 2" : interfaceName;

    if (!QFileInfo::exists(program)) {
        qWarning() << "Tshark executable not found, skipping capture bootstrap:" << program;
        return;
    }

    QStringList arguments = {
        "-i", deviceName,
        "-l",
        "-n",
        "-b", "filesize:3145728",
        "-b", "files:3",
        "-w", "E:/sharkfile/capture.pcap"
    };

    qDebug() << "Starting tshark with program:" << program << "arguments:" << arguments;
    tsharkProcess->start(program, arguments);

    connect(tsharkProcess, &QProcess::readyReadStandardError, this, [=]() {
        QByteArray errorOutput = tsharkProcess->readAllStandardError();
        if (!errorOutput.isEmpty()) {
            qWarning() << "Tshark Error Output:" << errorOutput;
        }
    });

    if (!tsharkProcess->waitForStarted()) {
        qWarning() << "Failed to start tshark. Error:" << tsharkProcess->errorString();
    } else {
        qDebug() << "Tshark started successfully.";
    }
}

void UdpReceiver::readPendingDatagrams() {
    QList<QByteArray> batch;
    batch.reserve(kMaxBatchPackets);

    while (mrecv->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(mrecv->pendingDatagramSize());
        const qint64 bytesRead = mrecv->readDatagram(datagram.data(), datagram.size());
        if (bytesRead <= 0) {
            qWarning() << "Failed to read UDP datagram:" << mrecv->errorString();
            continue;
        }

        if (!datagram.isEmpty()) {
            batch.push_back(datagram);
        }

        if (batch.size() >= kMaxBatchPackets) {
            emit newFrameBatch(batch);
            batch.clear();
        }
    }

    if (!batch.isEmpty()) {
        emit newFrameBatch(batch);
    }
}
