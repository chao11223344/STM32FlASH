#include "dfuflasher.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>

DfuFlasher::DfuFlasher(QObject *parent) : QObject(parent)
{
    m_cli = autodetectCli();
}

QString DfuFlasher::autodetectCli()
{
    static const QStringList candidates = {
        "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe",
        "C:/Program Files (x86)/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe",
    };
    for (const QString &c : candidates)
        if (QFileInfo::exists(c))
            return c;

    const QString onPath = QStandardPaths::findExecutable("STM32_Programmer_CLI");
    return onPath; // 可能为空
}

bool DfuFlasher::isRunning() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

void DfuFlasher::flash(const QString &firmware, quint32 address)
{
    if (isRunning()) {
        emit finished(false, tr("已有烧录任务在进行中"));
        return;
    }
    if (m_cli.isEmpty() || !QFileInfo::exists(m_cli)) {
        emit finished(false, tr("未找到 STM32_Programmer_CLI.exe，请在烧录界面指定其路径。"));
        return;
    }
    if (!QFileInfo::exists(firmware)) {
        emit finished(false, tr("固件文件不存在: %1").arg(firmware));
        return;
    }

    const QString ext = QFileInfo(firmware).suffix().toLower();
    quint32 runAddr = 0x08000000;         // 应用入口 = Flash 基址 (向量表所在)
    QStringList args;
    args << "-c" << "port=usb1";          // 连接到第一个 USB DFU 设备
    if (m_fullErase)
        args << "-e" << "all";          // 全盘擦除: 写入前擦除所有扇区 (须在 -w 之前)
    args << "-w" << QDir::toNativeSeparators(firmware);
    if (ext == "bin") {                   // 仅 .bin 需要地址
        args << QString("0x%1").arg(address, 0, 16);
        runAddr = address;
    }
    args << "-v";                         // 写后校验
    // DFU 没有硬件复位; 用 -g 离开 DFU 并跳转运行 (不能用 -rst, 那是 JTAG/SWD 专用)
    args << "-g" << QString("0x%1").arg(runAddr, 0, 16);

    m_downloadOk = false;
    m_phase = PhaseErase;
    m_lineBuf.clear();

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        handleChunk(QString::fromLocal8Bit(m_proc->readAllStandardOutput()));
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus st) {
        if (!m_lineBuf.isEmpty()) { handleChunk("\n"); }
        // 以「写入并校验成功」为准; -g 跳转那步即使报警也不影响已写入的固件
        const bool ok = m_downloadOk;
        QString msg;
        if (ok && st == QProcess::NormalExit && code == 0)
            msg = tr("烧录成功，已跳转运行。");
        else if (ok)
            msg = tr("烧录成功 (已写入并校验)。若飞控未自动运行，请断电重新上电。");
        else
            msg = tr("烧录失败 (退出码 %1)。详见日志。").arg(code);
        emit finished(ok, msg);
        m_proc->deleteLater();
        m_proc = nullptr;
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit logLine(tr("[进程错误] %1").arg(m_proc->errorString()));
    });

    emit started();
    emit logLine(QString("> \"%1\" %2").arg(m_cli, args.join(' ')));
    m_proc->start(m_cli, args);
}

void DfuFlasher::cancel()
{
    if (isRunning()) {
        m_proc->kill();
        emit logLine(tr("[已取消]"));
    }
}

void DfuFlasher::setPhase(int phase)
{
    if (phase != m_phase) {
        m_phase = phase;
        emit phaseChanged(phase);
    }
}

void DfuFlasher::handleChunk(const QString &text)
{
    m_lineBuf += text;
    // CubeProgrammer 用 \r 刷新进度行, 统一切分
    m_lineBuf.replace('\r', '\n');

    // 去掉进度条等非可见 ASCII 字符 (中文 Windows 下会显示成乱码)
    static const QRegularExpression rxNonAscii("[^\\x20-\\x7E]");

    int nl;
    while ((nl = m_lineBuf.indexOf('\n')) >= 0) {
        QString line = m_lineBuf.left(nl);
        m_lineBuf.remove(0, nl + 1);
        line.remove(rxNonAscii);
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        // 阶段切换 (顺序: 擦除 -> 写入 -> 校验)
        if (line.contains("Erasing", Qt::CaseInsensitive))
            setPhase(PhaseErase);
        else if (line.contains("Download in Progress", Qt::CaseInsensitive))
            setPhase(PhaseWrite);
        else if (line.contains("Verifying", Qt::CaseInsensitive) ||
                 line.contains("Read progress", Qt::CaseInsensitive))
            setPhase(PhaseVerify);

        // 完成标记
        if (line.contains("File download complete", Qt::CaseInsensitive))
            emit progress(PhaseWrite, 100);
        if (line.contains("verified successfully", Qt::CaseInsensitive) ||
            line.contains("download complete", Qt::CaseInsensitive)) {
            m_downloadOk = true;
            emit progress(PhaseVerify, 100);
        }

        // 百分比 -> 当前阶段
        static const QRegularExpression rxPct("(\\d{1,3})%");
        const auto m = rxPct.match(line);
        if (m.hasMatch())
            emit progress(m_phase, qBound(0, m.captured(1).toInt(), 100));

        emit logLine(line);
    }
}
