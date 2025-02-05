#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include <QObject>
#include <QUdpSocket>
#include <QProcess>
#include <QTimer>

class UdpReceiver : public QObject {
    Q_OBJECT
public:
    explicit UdpReceiver(QObject *parent = nullptr);

    // Start receiving UDP data
    void startReceiving(const QString &address, quint16 port);

    // Start Tshark to keep the network interface active
    void startTshark(const QString &interfaceName = QString());

    virtual ~UdpReceiver();

signals:
    // Signal emitted when new frame data is received
    void newFrameData(const QByteArray &data);

private slots:
    // Clear the buffer periodically
    void clearBuffer();

    // Process incoming UDP packets
    void readPendingDatagrams();

private:
    QUdpSocket *mrecv;       // UDP socket for receiving data
    QProcess *tsharkProcess; // Tshark process for network monitoring
    QTimer *bufferCleaner;   // Timer to periodically clear the buffer
};

#endif // UDP_RECEIVER_H

