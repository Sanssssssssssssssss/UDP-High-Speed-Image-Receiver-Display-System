#ifndef FT601_RECEIVER_H
#define FT601_RECEIVER_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QLibrary>

class Ft601Receiver : public QObject {
    Q_OBJECT
public:
    explicit Ft601Receiver(QObject *parent = nullptr);
    ~Ft601Receiver();

signals:
    void newFrameBatch(const QList<QByteArray> &batch);
    void receiverStatusChanged(bool ok, const QString &message);

public slots:
    void startReceiving(const QString &deviceMatch, int pipeId, int transferBytes);
    void stopReceiving();

private:
    static const int kLogicalPacketBytes = 804;
    static const int kMaxBatchPackets = 1024;

    int findSyncOffset() const;
    void ingestStreamChunk(const QByteArray &chunk);
    bool ensureRuntimeAvailable(QString *message);

    QLibrary d3xxLibrary;
    QString currentDeviceMatch;
    int currentPipeId;
    int currentTransferBytes;
    QByteArray streamBuffer;
    bool running;
};

#endif // FT601_RECEIVER_H
