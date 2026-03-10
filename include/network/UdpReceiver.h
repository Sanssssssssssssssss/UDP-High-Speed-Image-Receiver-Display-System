#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include <QList>
#include <QObject>
#include <QProcess>
#include <QUdpSocket>

class UdpReceiver : public QObject {
    Q_OBJECT
public:
    explicit UdpReceiver(QObject *parent = nullptr);

    // Start Tshark to keep the network interface active
    void startTshark(const QString &interfaceName = QString());

    virtual ~UdpReceiver();

signals:
    // Signal emitted when new frame data is received
    void newFrameBatch(const QList<QByteArray> &batch);
    void receiverBindingChanged(const QString &address, quint16 port, bool ok, const QString &message);

public slots:
    // Start or restart receiving UDP data
    void startReceiving(const QString &address, quint16 port);
    void stopReceiving();

private slots:
    // Process incoming UDP packets
    void readPendingDatagrams();

private:
    QUdpSocket *mrecv;       // UDP socket for receiving data
    QProcess *tsharkProcess; // Tshark process for network monitoring
    QString boundAddress;
    quint16 boundPort;
};

#endif // UDP_RECEIVER_H

