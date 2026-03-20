#include "Ft601Receiver.h"
#include <QDebug>
#include <cstring>

namespace {
const char kSyncHeader[4] = { 0x55, 0x33, 0x11, 0x77 };
}

Ft601Receiver::Ft601Receiver(QObject *parent)
    : QObject(parent),
      currentPipeId(0x82),
      currentTransferBytes(16384),
      running(false) {
}

Ft601Receiver::~Ft601Receiver() {
    stopReceiving();
}

void Ft601Receiver::startReceiving(const QString &deviceMatch, int pipeId, int transferBytes) {
    stopReceiving();

    currentDeviceMatch = deviceMatch.trimmed();
    currentPipeId = pipeId;
    currentTransferBytes = transferBytes;

    QString runtimeMessage;
    if (!ensureRuntimeAvailable(&runtimeMessage)) {
        emit receiverStatusChanged(false, runtimeMessage);
        return;
    }

    running = true;
    streamBuffer.clear();

    emit receiverStatusChanged(
        true,
        QString("FT601 USB ingress scaffold ready | device match=%1 | pipe=0x%2 | transfer=%3 bytes | waiting for D3XX read-loop integration/hardware stream")
            .arg(currentDeviceMatch.isEmpty() ? QString("<any>") : currentDeviceMatch)
            .arg(currentPipeId, 2, 16, QLatin1Char('0'))
            .arg(currentTransferBytes));
}

void Ft601Receiver::stopReceiving() {
    running = false;
    streamBuffer.clear();
}

bool Ft601Receiver::ensureRuntimeAvailable(QString *message) {
    if (d3xxLibrary.isLoaded()) {
        return true;
    }

    d3xxLibrary.setFileName("FTD3XX");
    if (!d3xxLibrary.load()) {
        if (message != nullptr) {
            *message = "FT601 USB mode requires FTD3XX.dll. Install the FTDI D3XX runtime/driver to enable USB ingress.";
        }
        return false;
    }

    if (message != nullptr) {
        *message = "FTD3XX runtime detected.";
    }
    return true;
}

int Ft601Receiver::findSyncOffset() const {
    for (int i = 0; i <= streamBuffer.size() - 4; ++i) {
        if (std::memcmp(streamBuffer.constData() + i, kSyncHeader, 4) == 0) {
            return i;
        }
    }
    return -1;
}

void Ft601Receiver::ingestStreamChunk(const QByteArray &chunk) {
    if (!running || chunk.isEmpty()) {
        return;
    }

    streamBuffer.append(chunk);
    QList<QByteArray> batch;
    batch.reserve(kMaxBatchPackets);

    while (streamBuffer.size() >= 4) {
        const int syncOffset = findSyncOffset();
        if (syncOffset < 0) {
            if (streamBuffer.size() > 3) {
                streamBuffer.remove(0, streamBuffer.size() - 3);
            }
            break;
        }

        if (syncOffset > 0) {
            streamBuffer.remove(0, syncOffset);
        }

        if (streamBuffer.size() < kLogicalPacketBytes) {
            break;
        }

        const QByteArray packet = streamBuffer.left(kLogicalPacketBytes);
        if (std::memcmp(packet.constData(), kSyncHeader, 4) != 0) {
            streamBuffer.remove(0, 1);
            continue;
        }

        batch.push_back(packet);
        streamBuffer.remove(0, kLogicalPacketBytes);

        if (batch.size() >= kMaxBatchPackets) {
            emit newFrameBatch(batch);
            batch.clear();
        }
    }

    if (!batch.isEmpty()) {
        emit newFrameBatch(batch);
    }
}
