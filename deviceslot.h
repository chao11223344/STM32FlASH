#ifndef DEVICESLOT_H
#define DEVICESLOT_H

#include <QWidget>
#include <QElapsedTimer>
#include "flash/dfudevlist.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QPushButton;
class QPropertyAnimation;
class QTimer;
QT_END_NAMESPACE

class DfuFlasher;

// 单个 DFU 设备的烧录槽位: 标题(usbN+SN) + 状态 + 擦除/写入进度 + 结果 + 日志 + 烧录按钮。
// 自带烧录状态机 (阶段进度/看门狗/耗时), 各槽位独立并发。
class DeviceSlot : public QWidget {
    Q_OBJECT
public:
    explicit DeviceSlot(const DfuDevice &dev, QWidget *parent = nullptr);

    QString sn() const { return m_dev.sn; }
    QString port() const { return m_dev.usbN; }
    bool isFlashing() const { return m_flashing; }
    bool isFinished() const { return m_finished; }  // 已完成(成功/失败), 保留显示结果
    // 烧录完成后设备已离开 DFU: 同 SN 再次进 DFU 则复位重烧。
    bool awaitingReinsert() const { return m_finished && m_deviceLeft; }
    void markDeviceLeft();     // 标记设备已离开 (完成后调用)
    void prepareReflash();     // 复位到就绪, 准备再次烧录

    // 共享设置 (由主窗口下发)
    void setFirmwarePath(const QString &p) { m_firmware = p; updateButtonState(); }
    void setCliPath(const QString &p);
    void setFullErase(bool on) { m_fullErase = on; }

    // 设备信息更新 (usbN 可能随插拔变; 烧录中不改 port)
    void setDevice(const DfuDevice &dev);

    // 该设备在烧录中消失 (主窗口检测到 SN 不在列表) -> 启动看门狗
    void onDeviceGone();

    void startFlash();

signals:
    // 一次烧录结束 (无论成功失败), 主窗口据此恢复轮询
    void flashFinished();

private slots:
    void onStarted();
    void onPhase(int phase);
    void onProgress(int phase, int percent);
    void onLog(const QString &line);
    void onFinished(bool ok, const QString &message);

private:
    void buildUi();
    void animateBar(QProgressBar *bar, QPropertyAnimation *anim, int target, int durMs);
    void startWriteAnim();
    void startWatchdog();
    void clearProgress();
    void updateButtonState();

    DfuDevice m_dev;
    DfuFlasher *m_flasher = nullptr;
    QString m_firmware;
    bool m_fullErase = false;

    QLabel *m_title = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_eraseBar = nullptr;
    QProgressBar *m_writeBar = nullptr;
    QLabel *m_result = nullptr;
    QPushButton *m_flashBtn = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_logToggle = nullptr;
    QPropertyAnimation *m_eraseAnim = nullptr;
    QPropertyAnimation *m_writeAnim = nullptr;
    QTimer *m_watchdog = nullptr;

    QElapsedTimer m_timer;
    bool m_flashing = false;
    bool m_finished = false;   // 一次烧录已完成 (保留槽位显示结果, 不自动移除)
    bool m_deviceLeft = false;  // 完成后设备已离开 DFU (同 SN 再出现则复位重烧)
    bool m_writeAnimStarted = false;
    bool m_usbLost = false;
    int m_estEraseMs = 3000;
    int m_estWriteMs = 15000;
};

#endif // DEVICESLOT_H
