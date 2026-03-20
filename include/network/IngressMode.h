#ifndef INGRESS_MODE_H
#define INGRESS_MODE_H

namespace IngressMode {
enum Type {
    UdpSocket = 0,
    NpcapDiagnostic = 1,
    Ft601Usb = 2
};
}

#endif // INGRESS_MODE_H
