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
#include <QLibrary>
#include <QNetworkInterface>
#include <QHostAddress>
#include <atomic>
#include <cstring>
#include <thread>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace {
const int kDesiredReceiveBufferBytes = 16 * 1024 * 1024;
const int kMaxBatchPackets = 1024;

#ifdef Q_OS_WIN
const int kPcapErrbufSize = 256;
const unsigned int kEtherTypeIpv4 = 0x0800;
const unsigned int kEtherTypeVlan = 0x8100;
const unsigned int kEtherTypeQinQ = 0x88A8;
const int kEthernetHeaderBytes = 14;
const int kUdpHeaderBytes = 8;
const unsigned int kPcapNetmaskUnknown = 0xffffffffU;

struct pcap;
struct bpf_program {
    unsigned int bf_len;
    void *bf_insns;
};
struct pcap_if;
struct pcap_addr {
    pcap_addr *next;
    sockaddr *addr;
    sockaddr *netmask;
    sockaddr *broadaddr;
    sockaddr *dstaddr;
};
struct pcap_if {
    pcap_if *next;
    char *name;
    char *description;
    pcap_addr *addresses;
    unsigned int flags;
};
struct pcap_pkthdr {
    timeval ts;
    unsigned int caplen;
    unsigned int len;
};
typedef void (__cdecl *pcap_handler)(unsigned char *, const pcap_pkthdr *, const unsigned char *);
}

class NpcapCaptureSession {
public:
    explicit NpcapCaptureSession(UdpReceiver *owner)
        : owner(owner),
          handle(nullptr),
          filterPort(0),
          running(false) {
    }

    ~NpcapCaptureSession() {
        stop();
    }

    bool start(const QString &interfaceHint, quint16 port, QString *message) {
        stop();

        QString error;
        if (!loadApi(&error)) {
            if (message != nullptr) {
                *message = error;
            }
            return false;
        }

        char errbuf[kPcapErrbufSize] = {};
        QString deviceName;
        QString deviceDescription;
        if (!resolveDevice(interfaceHint, errbuf, &deviceName, &deviceDescription)) {
            if (message != nullptr) {
                *message = QString("Npcap could not resolve interface '%1': %2").arg(interfaceHint, QString::fromLocal8Bit(errbuf));
            }
            return false;
        }

        const QByteArray deviceNameBytes = deviceName.toLocal8Bit();
        handle = api.pcap_open_live(deviceNameBytes.constData(), 65536, 1, 20, errbuf);
        if (handle == nullptr) {
            if (message != nullptr) {
                *message = QString("Npcap open failed for '%1': %2").arg(deviceDescription, QString::fromLocal8Bit(errbuf));
            }
            return false;
        }

        if (api.pcap_setmintocopy != nullptr) {
            api.pcap_setmintocopy(handle, 0);
        }

        bpf_program program = {};
        const QByteArray filter = QString("udp and dst port %1").arg(port).toLocal8Bit();
        if (api.pcap_compile(handle, &program, filter.constData(), 1, kPcapNetmaskUnknown) != 0) {
            const QString compileError = QString::fromLocal8Bit(api.pcap_geterr(handle));
            api.pcap_close(handle);
            handle = nullptr;
            if (message != nullptr) {
                *message = QString("Npcap filter compile failed: %1").arg(compileError);
            }
            return false;
        }

        if (api.pcap_setfilter(handle, &program) != 0) {
            const QString filterError = QString::fromLocal8Bit(api.pcap_geterr(handle));
            api.pcap_freecode(&program);
            api.pcap_close(handle);
            handle = nullptr;
            if (message != nullptr) {
                *message = QString("Npcap filter apply failed: %1").arg(filterError);
            }
            return false;
        }
        api.pcap_freecode(&program);

        captureInterfaceHint = interfaceHint;
        captureDeviceDescription = deviceDescription;
        filterPort = port;
        running.store(true);
        captureThread = std::thread(&NpcapCaptureSession::captureLoop, this);

        if (message != nullptr) {
            *message = QString("Npcap capture active on %1 | UDP dport=%2 | promiscuous=on")
                           .arg(deviceDescription)
                           .arg(port);
        }
        return true;
    }

    void stop() {
        running.store(false);

        if (handle != nullptr && api.pcap_breakloop != nullptr) {
            api.pcap_breakloop(handle);
        }

        if (captureThread.joinable()) {
            captureThread.join();
        }

        if (handle != nullptr && api.pcap_close != nullptr) {
            api.pcap_close(handle);
            handle = nullptr;
        }
    }

private:
    struct Api {
        QLibrary wpcap;
        typedef pcap *(__cdecl *pcap_open_live_fn)(const char *, int, int, int, char *);
        typedef void (__cdecl *pcap_close_fn)(pcap *);
        typedef int (__cdecl *pcap_compile_fn)(pcap *, bpf_program *, const char *, int, unsigned int);
        typedef int (__cdecl *pcap_setfilter_fn)(pcap *, bpf_program *);
        typedef void (__cdecl *pcap_freecode_fn)(bpf_program *);
        typedef int (__cdecl *pcap_dispatch_fn)(pcap *, int, pcap_handler, unsigned char *);
        typedef int (__cdecl *pcap_findalldevs_fn)(pcap_if **, char *);
        typedef void (__cdecl *pcap_freealldevs_fn)(pcap_if *);
        typedef char *(__cdecl *pcap_geterr_fn)(pcap *);
        typedef void (__cdecl *pcap_breakloop_fn)(pcap *);
        typedef int (__cdecl *pcap_setmintocopy_fn)(pcap *, int);

        pcap_open_live_fn pcap_open_live = nullptr;
        pcap_close_fn pcap_close = nullptr;
        pcap_compile_fn pcap_compile = nullptr;
        pcap_setfilter_fn pcap_setfilter = nullptr;
        pcap_freecode_fn pcap_freecode = nullptr;
        pcap_dispatch_fn pcap_dispatch = nullptr;
        pcap_findalldevs_fn pcap_findalldevs = nullptr;
        pcap_freealldevs_fn pcap_freealldevs = nullptr;
        pcap_geterr_fn pcap_geterr = nullptr;
        pcap_breakloop_fn pcap_breakloop = nullptr;
        pcap_setmintocopy_fn pcap_setmintocopy = nullptr;
    } api;

    static quint16 readBigEndian16(const unsigned char *ptr) {
        return static_cast<quint16>((static_cast<quint16>(ptr[0]) << 8) | static_cast<quint16>(ptr[1]));
    }

    bool loadApi(QString *message) {
        if (api.pcap_open_live != nullptr) {
            return true;
        }

        api.wpcap.setFileName("wpcap");
        if (!api.wpcap.load()) {
            if (message != nullptr) {
                *message = QString("Npcap/Wpcap runtime not found. Install Npcap to enable raw capture diagnostics.");
            }
            return false;
        }

        api.pcap_open_live = reinterpret_cast<Api::pcap_open_live_fn>(api.wpcap.resolve("pcap_open_live"));
        api.pcap_close = reinterpret_cast<Api::pcap_close_fn>(api.wpcap.resolve("pcap_close"));
        api.pcap_compile = reinterpret_cast<Api::pcap_compile_fn>(api.wpcap.resolve("pcap_compile"));
        api.pcap_setfilter = reinterpret_cast<Api::pcap_setfilter_fn>(api.wpcap.resolve("pcap_setfilter"));
        api.pcap_freecode = reinterpret_cast<Api::pcap_freecode_fn>(api.wpcap.resolve("pcap_freecode"));
        api.pcap_dispatch = reinterpret_cast<Api::pcap_dispatch_fn>(api.wpcap.resolve("pcap_dispatch"));
        api.pcap_findalldevs = reinterpret_cast<Api::pcap_findalldevs_fn>(api.wpcap.resolve("pcap_findalldevs"));
        api.pcap_freealldevs = reinterpret_cast<Api::pcap_freealldevs_fn>(api.wpcap.resolve("pcap_freealldevs"));
        api.pcap_geterr = reinterpret_cast<Api::pcap_geterr_fn>(api.wpcap.resolve("pcap_geterr"));
        api.pcap_breakloop = reinterpret_cast<Api::pcap_breakloop_fn>(api.wpcap.resolve("pcap_breakloop"));
        api.pcap_setmintocopy = reinterpret_cast<Api::pcap_setmintocopy_fn>(api.wpcap.resolve("pcap_setmintocopy"));

        const bool ok = api.pcap_open_live != nullptr
            && api.pcap_close != nullptr
            && api.pcap_compile != nullptr
            && api.pcap_setfilter != nullptr
            && api.pcap_freecode != nullptr
            && api.pcap_dispatch != nullptr
            && api.pcap_findalldevs != nullptr
            && api.pcap_freealldevs != nullptr
            && api.pcap_geterr != nullptr;
        if (!ok) {
            if (message != nullptr) {
                *message = "Npcap/Wpcap runtime is present but missing required capture exports.";
            }
            api.wpcap.unload();
            api.pcap_open_live = nullptr;
            api.pcap_close = nullptr;
            api.pcap_compile = nullptr;
            api.pcap_setfilter = nullptr;
            api.pcap_freecode = nullptr;
            api.pcap_dispatch = nullptr;
            api.pcap_findalldevs = nullptr;
            api.pcap_freealldevs = nullptr;
            api.pcap_geterr = nullptr;
            api.pcap_breakloop = nullptr;
            api.pcap_setmintocopy = nullptr;
            return false;
        }

        return true;
    }

    static QString socketAddressToString(const sockaddr *addr) {
        if (addr == nullptr) {
            return QString();
        }

        if (addr->sa_family == AF_INET) {
            const sockaddr_in *addr4 = reinterpret_cast<const sockaddr_in *>(addr);
            return QHostAddress(ntohl(addr4->sin_addr.s_addr)).toString();
        }

        if (addr->sa_family == AF_INET6) {
            const sockaddr_in6 *addr6 = reinterpret_cast<const sockaddr_in6 *>(addr);
            Q_IPV6ADDR qtAddr = {};
            std::memcpy(qtAddr.c, &addr6->sin6_addr, sizeof(qtAddr.c));
            return QHostAddress(qtAddr).toString();
        }

        return QString();
    }

    static bool interfaceNameMatches(const QNetworkInterface &iface, const QString &hint) {
        if (hint.isEmpty()) {
            return false;
        }

        return iface.humanReadableName().compare(hint, Qt::CaseInsensitive) == 0
            || iface.name().compare(hint, Qt::CaseInsensitive) == 0
            || iface.humanReadableName().contains(hint, Qt::CaseInsensitive)
            || iface.name().contains(hint, Qt::CaseInsensitive);
    }

    QStringList resolveHintAddresses(const QString &interfaceHint) const {
        QStringList addresses;
        if (interfaceHint.isEmpty()) {
            return addresses;
        }

        const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
        for (QList<QNetworkInterface>::const_iterator it = interfaces.cbegin(); it != interfaces.cend(); ++it) {
            if (!interfaceNameMatches(*it, interfaceHint)) {
                continue;
            }

            const QList<QNetworkAddressEntry> entries = it->addressEntries();
            for (QList<QNetworkAddressEntry>::const_iterator entryIt = entries.cbegin(); entryIt != entries.cend(); ++entryIt) {
                const QString ip = entryIt->ip().toString();
                if (!ip.isEmpty()) {
                    addresses.append(ip);
                }
            }
        }

        return addresses;
    }

    bool resolveDevice(const QString &interfaceHint,
                       char *errbuf,
                       QString *deviceNameOut,
                       QString *deviceDescriptionOut) {
        pcap_if *allDevices = nullptr;
        if (api.pcap_findalldevs(&allDevices, errbuf) != 0 || allDevices == nullptr) {
            return false;
        }

        const QStringList hintAddresses = resolveHintAddresses(interfaceHint);
        int bestScore = -1;
        QString bestName;
        QString bestDescription;

        for (pcap_if *dev = allDevices; dev != nullptr; dev = dev->next) {
            const QString name = QString::fromLocal8Bit(dev->name != nullptr ? dev->name : "");
            const QString description = QString::fromLocal8Bit(dev->description != nullptr ? dev->description : "");

            int score = interfaceHint.isEmpty() ? 1 : 0;
            if (!interfaceHint.isEmpty()) {
                if (name.compare(interfaceHint, Qt::CaseInsensitive) == 0 || description.compare(interfaceHint, Qt::CaseInsensitive) == 0) {
                    score += 6;
                } else if (name.contains(interfaceHint, Qt::CaseInsensitive) || description.contains(interfaceHint, Qt::CaseInsensitive)) {
                    score += 3;
                }
            }

            for (pcap_addr *addr = dev->addresses; addr != nullptr; addr = addr->next) {
                const QString devAddress = socketAddressToString(addr->addr);
                if (!devAddress.isEmpty() && hintAddresses.contains(devAddress)) {
                    score += 8;
                }
            }

            if (score > bestScore) {
                bestScore = score;
                bestName = name;
                bestDescription = description.isEmpty() ? name : description;
            }
        }

        api.pcap_freealldevs(allDevices);

        if (bestScore < 0 || bestName.isEmpty()) {
            qsnprintf(errbuf, kPcapErrbufSize, "no matching Npcap device");
            return false;
        }

        *deviceNameOut = bestName;
        *deviceDescriptionOut = bestDescription;
        return true;
    }

    static void __cdecl packetHandler(unsigned char *user, const pcap_pkthdr *header, const unsigned char *bytes) {
        if (user == nullptr || header == nullptr || bytes == nullptr) {
            return;
        }

        reinterpret_cast<NpcapCaptureSession *>(user)->handlePacket(bytes, header->caplen);
    }

    void flushBatch() {
        if (batchedPayloads.isEmpty()) {
            return;
        }

        emit owner->newFrameBatch(batchedPayloads);
        batchedPayloads.clear();
    }

    void handlePacket(const unsigned char *bytes, unsigned int length) {
        if (length < static_cast<unsigned int>(kEthernetHeaderBytes)) {
            return;
        }

        int offset = 12;
        quint16 etherType = readBigEndian16(bytes + offset);
        int payloadOffset = kEthernetHeaderBytes;
        if (etherType == kEtherTypeVlan || etherType == kEtherTypeQinQ) {
            if (length < static_cast<unsigned int>(kEthernetHeaderBytes + 4)) {
                return;
            }
            etherType = readBigEndian16(bytes + 16);
            payloadOffset += 4;
        }

        if (etherType != kEtherTypeIpv4) {
            return;
        }

        if (length < static_cast<unsigned int>(payloadOffset + 20)) {
            return;
        }

        const unsigned char *ipHeader = bytes + payloadOffset;
        const int ipHeaderLength = static_cast<int>(ipHeader[0] & 0x0F) * 4;
        if (ipHeaderLength < 20 || length < static_cast<unsigned int>(payloadOffset + ipHeaderLength + kUdpHeaderBytes)) {
            return;
        }

        if (ipHeader[9] != 17) {
            return;
        }

        const unsigned char *udpHeader = ipHeader + ipHeaderLength;
        const quint16 dstPort = readBigEndian16(udpHeader + 2);
        if (filterPort != 0 && dstPort != filterPort) {
            return;
        }

        const quint16 udpLength = readBigEndian16(udpHeader + 4);
        if (udpLength < kUdpHeaderBytes) {
            return;
        }

        const int payloadLength = static_cast<int>(udpLength) - kUdpHeaderBytes;
        const unsigned char *udpPayload = udpHeader + kUdpHeaderBytes;
        const unsigned int packetEnd = static_cast<unsigned int>((udpPayload - bytes) + payloadLength);
        if (payloadLength <= 0 || packetEnd > length) {
            return;
        }

        batchedPayloads.push_back(QByteArray(reinterpret_cast<const char *>(udpPayload), payloadLength));
        if (batchedPayloads.size() >= kMaxBatchPackets) {
            flushBatch();
        }
    }

    void captureLoop() {
        while (running.load()) {
            const int rc = api.pcap_dispatch(handle, kMaxBatchPackets, &NpcapCaptureSession::packetHandler, reinterpret_cast<unsigned char *>(this));
            flushBatch();

            if (!running.load()) {
                break;
            }

            if (rc == 0) {
                continue;
            }

            if (rc == -2) {
                break;
            }

            if (rc < 0) {
                const QString error = handle != nullptr ? QString::fromLocal8Bit(api.pcap_geterr(handle)) : QString("unknown Npcap dispatch error");
                emit owner->receiverBindingChanged(captureInterfaceHint, filterPort, false, QString("Npcap capture error: %1").arg(error));
                break;
            }
        }
    }

    UdpReceiver *owner;
    pcap *handle;
    quint16 filterPort;
    QString captureInterfaceHint;
    QString captureDeviceDescription;
    std::atomic<bool> running;
    std::thread captureThread;
    QList<QByteArray> batchedPayloads;
};
#endif

UdpReceiver::UdpReceiver(QObject *parent)
    : QObject(parent),
      mrecv(new QUdpSocket(this)),
      tsharkProcess(new QProcess(this)),
      boundPort(0),
      usingNpcap(false),
      npcapSession(nullptr) {
    qRegisterMetaType<QList<QByteArray> >("QList<QByteArray>");
    qRegisterMetaType<quint16>("quint16");

    connect(mrecv, &QUdpSocket::readyRead, this, &UdpReceiver::readPendingDatagrams);
    connect(mrecv, &QUdpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError socketError) {
        Q_UNUSED(socketError);
        qWarning() << "UDP socket error:" << mrecv->errorString();
    });
}

UdpReceiver::~UdpReceiver() {
    stopReceiving();
    delete npcapSession;
    npcapSession = nullptr;

    if (tsharkProcess && tsharkProcess->state() == QProcess::Running) {
        tsharkProcess->terminate();
        if (!tsharkProcess->waitForFinished(3000)) {
            qWarning() << "Tshark did not exit gracefully, killing process.";
            tsharkProcess->kill();
            tsharkProcess->waitForFinished();
        }
    }
}

void UdpReceiver::startReceiving(const QString &address, quint16 port, bool useNpcap, const QString &npcapInterface) {
    QHostAddress maddr(address);

    if (!useNpcap && maddr.isNull()) {
        const QString message = QString("Bind failed: invalid address %1").arg(address);
        qWarning() << message;
        emit receiverBindingChanged(address, port, false, message);
        return;
    }

    stopReceiving();

    boundAddress = address;
    boundPort = port;
    usingNpcap = useNpcap;
    boundNpcapInterface = npcapInterface.trimmed();

#ifdef Q_OS_WIN
    if (usingNpcap) {
        if (npcapSession == nullptr) {
            npcapSession = new NpcapCaptureSession(this);
        }

        QString message;
        if (!npcapSession->start(boundNpcapInterface, port, &message)) {
            qWarning() << message;
            emit receiverBindingChanged(address, port, false, message);
            return;
        }

        qDebug() << message;
        emit receiverBindingChanged(address, port, true, message);
        return;
    }
#else
    Q_UNUSED(npcapInterface);
    if (usingNpcap) {
        const QString message = QString("Npcap capture is only available on Windows builds.");
        qWarning() << message;
        emit receiverBindingChanged(address, port, false, message);
        return;
    }
#endif

    if (!mrecv->bind(maddr, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        const QString message = QString("Bind failed on %1:%2 (%3)")
                                    .arg(address)
                                    .arg(port)
                                    .arg(mrecv->errorString());
        qWarning() << message;
        emit receiverBindingChanged(address, port, false, message);
        return;
    }
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
    if (npcapSession != nullptr) {
        npcapSession->stop();
    }

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
