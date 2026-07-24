#ifndef DFUWATCHER_H
#define DFUWATCHER_H

// 监视飞控 STM32 是否处于 DFU 模式 (USB VID=0x0483 PID=0xDF11)。
// Windows: 用 SetupAPI 轮询枚举 USB 设备硬件 ID。其它平台返回 false。

#include <QObject>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class DfuWatcher : public QObject {
    Q_OBJECT
public:
    static constexpr quint16 DFU_VID = 0x0483; // STMicroelectronics
    static constexpr quint16 DFU_PID = 0xDF11; // STM32 BOOTLOADER (DFU)

    explicit DfuWatcher(QObject *parent = nullptr);

    void start(int intervalMs = 800);
    void stop();
    bool isPresent() const { return m_present; }

    // 一次性检测当前是否有 DFU 设备在线
    static bool scanPresent();

signals:
    void presentChanged(bool present);

private slots:
    void poll();

private:
    QTimer *m_timer = nullptr;
    bool m_present = false;
};

#endif // DFUWATCHER_H
