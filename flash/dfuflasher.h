#ifndef DFUFLASHER_H
#define DFUFLASHER_H

// 通过 STM32CubeProgrammer CLI (STM32_Programmer_CLI.exe) 经 USB DFU 烧录飞控固件。
// 支持 .bin/.hex/.elf/.dfu; .bin 需指定起始地址 (默认 0x08000000)。

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

class DfuFlasher : public QObject {
    Q_OBJECT
public:
    enum Phase { PhaseErase = 0, PhaseWrite = 1, PhaseVerify = 2 };

    explicit DfuFlasher(QObject *parent = nullptr);

    // 自动探测 STM32_Programmer_CLI.exe (常见安装路径 + PATH)。找不到返回空。
    static QString autodetectCli();

    void setCliPath(const QString &path) { m_cli = path; }
    QString cliPath() const { return m_cli; }
    bool isRunning() const;

    // 全盘擦除: 开启则在写入前 -e [all] 擦除所有扇区 (默认关闭=只擦写所需扇区)。
    void setFullErase(bool on) { m_fullErase = on; }
    bool fullErase() const { return m_fullErase; }

    // 是否已写入并校验成功 (看门狗用它判断: 已成功则不因 USB 断开而误杀)。
    bool downloadOk() const { return m_downloadOk; }

    // 开始烧录。address 仅对 .bin 生效。
    void flash(const QString &firmware, quint32 address = 0x08000000);
    void cancel();

signals:
    void started();
    void logLine(const QString &line);
    void phaseChanged(int phase);            // Phase
    void progress(int phase, int percent);   // 当前阶段 0..100
    void finished(bool ok, const QString &message);

private:
    void handleChunk(const QString &text);
    void setPhase(int phase);

    QProcess *m_proc = nullptr;
    QString m_cli;
    QString m_lineBuf;
    int m_phase = PhaseErase;
    bool m_downloadOk = false; // 见到 "verified/download complete" 即视为写入成功
    bool m_fullErase = false;   // 全盘擦除开关
};

#endif // DFUFLASHER_H
