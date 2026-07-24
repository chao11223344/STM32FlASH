#include "deviceslot.h"
#include "flash/dfuflasher.h"

#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QFont>
#include <QTimer>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>

DeviceSlot::DeviceSlot(const DfuDevice &dev, QWidget *parent)
    : QWidget(parent), m_dev(dev)
{
    buildUi();

    m_flasher = new DfuFlasher(this);
    m_flasher->setPort(m_dev.usbN);
    connect(m_flasher, &DfuFlasher::started,      this, &DeviceSlot::onStarted);
    connect(m_flasher, &DfuFlasher::phaseChanged, this, &DeviceSlot::onPhase);
    connect(m_flasher, &DfuFlasher::progress,     this, &DeviceSlot::onProgress);
    connect(m_flasher, &DfuFlasher::logLine,      this, &DeviceSlot::onLog);
    connect(m_flasher, &DfuFlasher::finished,     this, &DeviceSlot::onFinished);

    m_eraseAnim = new QPropertyAnimation(m_eraseBar, "value", this);
    m_writeAnim = new QPropertyAnimation(m_writeBar, "value", this);

    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, [this] {
        if (m_flashing && m_flasher && m_flasher->isRunning() && !m_flasher->downloadOk()) {
            m_usbLost = true;
            m_flasher->cancel();   // 杀掉卡住的 CLI -> 触发 onFinished
        }
    });

    m_title->setText(QString("%1  ·  SN %2").arg(m_dev.usbN, m_dev.sn));
    updateButtonState();
}

void DeviceSlot::buildUi()
{
    QGroupBox *box = new QGroupBox;
    QVBoxLayout *v = new QVBoxLayout(box);

    m_title = new QLabel;
    m_title->setStyleSheet("font-weight:bold;");
    v->addWidget(m_title);

    QHBoxLayout *topRow = new QHBoxLayout;
    m_status = new QLabel(tr("就绪"));
    m_flashBtn = new QPushButton(tr("烧录"));
    connect(m_flashBtn, &QPushButton::clicked, this, &DeviceSlot::startFlash);
    topRow->addWidget(m_status, 1);
    topRow->addWidget(m_flashBtn);
    v->addLayout(topRow);

    QGridLayout *pg = new QGridLayout;
    m_eraseBar = new QProgressBar; m_eraseBar->setRange(0, 100); m_eraseBar->setValue(0);
    m_writeBar = new QProgressBar; m_writeBar->setRange(0, 100); m_writeBar->setValue(0);
    pg->addWidget(new QLabel(tr("擦除:")), 0, 0);
    pg->addWidget(m_eraseBar, 0, 1);
    pg->addWidget(new QLabel(tr("写入:")), 1, 0);
    pg->addWidget(m_writeBar, 1, 1);
    v->addLayout(pg);

    QHBoxLayout *resRow = new QHBoxLayout;
    m_result = new QLabel(tr("空闲"));
    m_result->setWordWrap(true);
    m_logToggle = new QPushButton(tr("日志 ▾"));
    m_logToggle->setCheckable(true);
    m_logToggle->setChecked(false);
    m_logToggle->setMaximumWidth(72);
    resRow->addWidget(m_result, 1);
    resRow->addWidget(m_logToggle);
    v->addLayout(resRow);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    m_log->setMaximumHeight(110);
    QFont lf("monospace"); lf.setPointSize(8); m_log->setFont(lf);
    m_log->setVisible(false);
    v->addWidget(m_log);
    connect(m_logToggle, &QPushButton::toggled, this, [this](bool on) {
        m_log->setVisible(on);
        m_logToggle->setText(on ? tr("日志 ▾") : tr("日志 ▸"));
    });

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);
}

void DeviceSlot::setCliPath(const QString &p)
{
    if (m_flasher) m_flasher->setCliPath(p);
}

void DeviceSlot::setDevice(const DfuDevice &dev)
{
    // port(usbN) 固定为槽位创建时的值, 不随枚举顺序变化改 (保持身份与烧录目标稳定)。
    // 仅更新 SN 显示 (USB 序列号通用, 仅作信息)。
    m_dev.sn = dev.sn;
    m_title->setText(QString("%1  ·  SN %2").arg(m_dev.usbN, m_dev.sn));
}

void DeviceSlot::onDeviceGone()
{
    if (m_flashing)
        startWatchdog();
}

void DeviceSlot::animateBar(QProgressBar *bar, QPropertyAnimation *anim, int target, int durMs)
{
    if (!bar || !anim) return;
    anim->stop();
    anim->setStartValue(bar->value());
    anim->setEndValue(qBound(0, target, 100));
    anim->setDuration(durMs);
    anim->setEasingCurve(QEasingCurve::Linear);
    anim->start();
}

void DeviceSlot::startWatchdog()
{
    if (m_watchdog) m_watchdog->start(3000);
}

void DeviceSlot::clearProgress()
{
    m_eraseAnim->stop();
    m_writeAnim->stop();
    m_eraseBar->setValue(0);
    m_writeBar->setValue(0);
    m_result->setText(tr("空闲"));
    m_result->setStyleSheet("");
}

void DeviceSlot::markDeviceLeft()
{
    if (m_finished)
        m_deviceLeft = true;   // 设备离开 DFU: 同 SN 再出现时复位重烧
}

void DeviceSlot::prepareReflash()
{
    m_finished = false;
    m_deviceLeft = false;
    clearProgress();
    m_status->setText(tr("就绪"));
    updateButtonState();
}

void DeviceSlot::updateButtonState()
{
    const bool valid = !m_firmware.isEmpty() && QFileInfo::exists(m_firmware);
    m_flashBtn->setEnabled(valid && !m_flashing);
    m_status->setText(valid ? (m_flashing ? tr("烧录中…") : tr("就绪"))
                            : tr("未选固件"));
}

void DeviceSlot::startFlash()
{
    if (m_flashing) return;
    const bool valid = !m_firmware.isEmpty() && QFileInfo::exists(m_firmware);
    if (!valid) {
        m_result->setText(tr("请先选择有效固件文件。"));
        m_result->setStyleSheet("color:#ff851b;");
        return;
    }
    m_finished = false;
    m_eraseBar->setValue(0);
    m_writeBar->setValue(0);
    m_log->clear();

    const qint64 sz = QFileInfo(m_firmware).size();
    const int kb = int(sz / 1024) + 1;
    m_estEraseMs = qBound(1500, kb * 8,  15000);
    m_estWriteMs = qBound(3000, kb * 40, 90000);

    m_flasher->setFullErase(m_fullErase);
    m_flasher->flash(m_firmware, 0x08000000);
}

void DeviceSlot::onStarted()
{
    m_flashing = true;
    m_writeAnimStarted = false;
    m_usbLost = false;
    m_timer.start();
    m_flashBtn->setEnabled(false);
    m_status->setText(tr("擦除中…"));
    m_result->setText(tr("擦除中…"));
    m_result->setStyleSheet("color:#0074d9;");

    m_eraseBar->setValue(0);
    m_writeBar->setValue(0);
    animateBar(m_eraseBar, m_eraseAnim, 95, m_estEraseMs);
    QTimer::singleShot(m_estEraseMs, this, [this] { startWriteAnim(); });
}

void DeviceSlot::startWriteAnim()
{
    if (!m_flashing || m_writeAnimStarted) return;
    m_writeAnimStarted = true;
    m_eraseAnim->stop();
    m_eraseBar->setValue(100);
    m_status->setText(tr("写入中…"));
    m_result->setText(tr("写入中…"));
    animateBar(m_writeBar, m_writeAnim, 95, m_estWriteMs);
}

void DeviceSlot::onPhase(int phase)
{
    if (phase == DfuFlasher::PhaseWrite) {
        startWriteAnim();
    } else if (phase == DfuFlasher::PhaseVerify) {
        m_writeAnim->stop();
        m_writeBar->setValue(100);
        m_status->setText(tr("校验中…"));
        m_result->setText(tr("校验中…"));
    }
}

void DeviceSlot::onProgress(int phase, int percent)
{
    if (percent >= 100 && phase == DfuFlasher::PhaseWrite) {
        m_writeAnim->stop();
        m_writeBar->setValue(100);
    }
}

void DeviceSlot::onLog(const QString &line)
{
    if (m_log) m_log->appendPlainText(line);
    if (line.contains("Error", Qt::CaseInsensitive)) {
        m_result->setText(line);
        m_result->setStyleSheet("color:#ff4136;");
    }
}

void DeviceSlot::onFinished(bool ok, const QString &message)
{
    if (m_watchdog) m_watchdog->stop();
    m_flashing = false;
    m_finished = true;
    m_writeAnimStarted = false;
    m_eraseAnim->stop();
    m_writeAnim->stop();

    bool finalOk = ok;
    QString finalMsg = message;
    if (m_usbLost) {
        finalOk = false;
        finalMsg = tr("USB 断开，烧录中断");
        m_usbLost = false;
    }

    if (finalOk) {
        m_eraseBar->setValue(100);
        m_writeBar->setValue(100);
        const double secs = m_timer.elapsed() / 1000.0;
        m_result->setText(QString("✔ %1（耗时 %2 秒）").arg(finalMsg).arg(secs, 0, 'f', 1));
        m_result->setStyleSheet("color:#2ecc40;font-weight:bold;");
        m_status->setText(tr("成功"));
    } else {
        m_result->setText("✘ " + finalMsg);
        m_result->setStyleSheet("color:#ff4136;font-weight:bold;");
        m_status->setText(tr("失败"));
    }
    updateButtonState();
    emit flashFinished();
}
