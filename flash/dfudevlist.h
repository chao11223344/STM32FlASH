#ifndef DFUDEVLIST_H
#define DFUDEVLIST_H

// 枚举所有在线的 STM32 DFU 设备 (USB VID=0x0483 PID=0xDF11)。
// Windows: 用 SetupAPI 轮询, 取每个设备的 SN (硬件 ID 末段)。
// usbN 按枚举顺序赋予 (usb1, usb2...), 供 CubeProgrammer -c port=usbN 定位。
//   注: 同一固件批量烧录时, usbN<->SN 配对是否严格对应不影响正确性 (每块烧同样固件)。
//   可在烧录进行中暂停轮询 (设备集稳定, 且避免干扰活跃烧录)。

#include <QObject>
#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

struct DfuDevice {
    QString usbN;  // "usb1", "usb2"... (定位用, 随插拔可能变)
    QString sn;     // USB 序列号 (芯片唯一 ID, 槽位稳定标识)
};

class DfuDeviceList : public QObject {
    Q_OBJECT
public:
    static constexpr quint16 DFU_VID = 0x0483; // STMicroelectronics
    static constexpr quint16 DFU_PID = 0xDF11; // STM32 BOOTLOADER (DFU)

    explicit DfuDeviceList(QObject *parent = nullptr);

    void start(int intervalMs = 1200);
    void stop();
    void setPaused(bool paused) { m_paused = paused; }
    QList<DfuDevice> devices() const { return m_devices; }

signals:
    void devicesChanged(const QList<DfuDevice> &devices);

private slots:
    void poll();

private:
    static QList<DfuDevice> scan();

    QTimer *m_timer = nullptr;
    QList<DfuDevice> m_devices;
    bool m_paused = false;
};

#endif // DFUDEVLIST_H
