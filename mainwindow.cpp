#include "mainwindow.h"
#include "flash/dfuwatcher.h"
#include "flash/dfuflasher.h"

#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QFont>
#include <QTimer>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSettings>
#include <QFileInfo>
#include <QFileDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("STM32固件烧录"));

    QWidget *central = new QWidget;
    QVBoxLayout *root = new QVBoxLayout(central);
    root->addWidget(buildPanel());
    root->addStretch(1);
    setCentralWidget(central);
    resize(640, 540);

    // 烧录器 + 路径 (自动探测, 可改; QSettings 持久化)
    m_flasher = new DfuFlasher(this);
    QString cliPath = QSettings().value("cubeProgrammerPath").toString();
    if (cliPath.isEmpty()) cliPath = m_flasher->cliPath();
    m_cliEdit->setText(cliPath);
    m_flasher->setCliPath(cliPath);
    connect(m_flasher, &DfuFlasher::started,      this, &MainWindow::onFlashStarted);
    connect(m_flasher, &DfuFlasher::phaseChanged, this, &MainWindow::onFlashPhase);
    connect(m_flasher, &DfuFlasher::progress,     this, &MainWindow::onFlashProgress);
    connect(m_flasher, &DfuFlasher::logLine,      this, &MainWindow::onFlashLog);
    connect(m_flasher, &DfuFlasher::finished,     this, &MainWindow::onFlashFinished);

    m_eraseAnim = new QPropertyAnimation(m_eraseBar, "value", this);
    m_writeAnim = new QPropertyAnimation(m_writeBar, "value", this);

    // 烧录中 DFU 消失的看门狗: 多数情况是末尾 -g 让飞控重启(进程会很快退出);
    // 若 CLI 卡住不退出且未完成校验, 判定为中途拔出, 杀进程并报 USB 断开。
    m_flashWatchdog = new QTimer(this);
    m_flashWatchdog->setSingleShot(true);
    connect(m_flashWatchdog, &QTimer::timeout, this, [this] {
        if (m_flashing && m_flasher && m_flasher->isRunning() && !m_flasher->downloadOk()) {
            m_usbLost = true;
            m_flasher->cancel();   // 杀掉卡住的 CLI -> 触发 onFlashFinished
        }
    });

    m_dfuWatcher = new DfuWatcher(this);
    connect(m_dfuWatcher, &DfuWatcher::presentChanged, this, &MainWindow::onDfuPresentChanged);

    setFirmware(QSettings().value("lastFirmware").toString());
    m_dfuWatcher->start();
    onDfuPresentChanged(m_dfuWatcher->isPresent());
}

MainWindow::~MainWindow() = default;

QWidget *MainWindow::buildPanel()
{
    QGroupBox *box = new QGroupBox(tr("固件烧录 (DFU)"));
    QVBoxLayout *v = new QVBoxLayout(box);

    // 固件选择
    QHBoxLayout *fwRow = new QHBoxLayout;
    fwRow->addWidget(new QLabel(tr("固件:")));
    m_fwEdit = new QLineEdit;
    m_fwEdit->setReadOnly(true);
    m_fwBrowse = new QPushButton(tr("浏览…"));
    connect(m_fwBrowse, &QPushButton::clicked, this, &MainWindow::browseFirmware);
    fwRow->addWidget(m_fwEdit, 1);
    fwRow->addWidget(m_fwBrowse);
    v->addLayout(fwRow);

    // DFU 状态 + 自动烧录 + 全盘擦除 + 手动
    QHBoxLayout *ctlRow = new QHBoxLayout;
    m_dfuStatus = new QLabel;
    m_flashArm = new QCheckBox(tr("检测到 DFU 即自动烧录"));
    m_flashArm->setChecked(QSettings().value("flashAutoArm", true).toBool());
    connect(m_flashArm, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("flashAutoArm", on);
        maybeAutoFlash();
    });
    m_fullErase = new QCheckBox(tr("全盘擦除"));
    m_fullErase->setToolTip(tr("开启则在写入前擦除整片 Flash (默认关闭: 只擦写所需扇区)"));
    m_fullErase->setChecked(QSettings().value("fullErase", false).toBool());
    connect(m_fullErase, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue("fullErase", on);
    });
    m_flashBtn = new QPushButton(tr("立即烧录"));
    connect(m_flashBtn, &QPushButton::clicked, this, &MainWindow::startFlash);
    ctlRow->addWidget(m_dfuStatus, 1);
    ctlRow->addWidget(m_flashArm);
    ctlRow->addWidget(m_fullErase);
    ctlRow->addWidget(m_flashBtn);
    v->addLayout(ctlRow);

    // 两条进度条: 擦除 / 写入
    QGridLayout *pg = new QGridLayout;
    m_eraseBar = new QProgressBar; m_eraseBar->setRange(0, 100); m_eraseBar->setValue(0);
    m_writeBar = new QProgressBar; m_writeBar->setRange(0, 100); m_writeBar->setValue(0);
    pg->addWidget(new QLabel(tr("擦除:")), 0, 0);
    pg->addWidget(m_eraseBar, 0, 1);
    pg->addWidget(new QLabel(tr("写入:")), 1, 0);
    pg->addWidget(m_writeBar, 1, 1);
    v->addLayout(pg);

    QHBoxLayout *resRow = new QHBoxLayout;
    m_flashResult = new QLabel(tr("空闲"));
    m_flashResult->setWordWrap(true);
    m_logToggle = new QPushButton(tr("日志 ▾"));
    m_logToggle->setCheckable(true);
    m_logToggle->setChecked(true);
    m_logToggle->setMaximumWidth(72);
    resRow->addWidget(m_flashResult, 1);
    resRow->addWidget(m_logToggle);
    v->addLayout(resRow);

    // 可折叠小日志框
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setMaximumHeight(160);
    QFont lf("monospace"); lf.setPointSize(8); m_log->setFont(lf);
    v->addWidget(m_log);
    connect(m_logToggle, &QPushButton::toggled, this, [this](bool on) {
        m_log->setVisible(on);
        m_logToggle->setText(on ? tr("日志 ▾") : tr("日志 ▸"));
    });

    // CubeProgrammer 路径 (自动探测, 可改)
    QHBoxLayout *cliRow = new QHBoxLayout;
    cliRow->addWidget(new QLabel("CLI:"));
    m_cliEdit = new QLineEdit;
    m_cliEdit->setPlaceholderText(tr("STM32_Programmer_CLI.exe 路径"));
    m_cliBrowse = new QPushButton(tr("…"));
    m_cliBrowse->setMaximumWidth(32);
    connect(m_cliBrowse, &QPushButton::clicked, this, &MainWindow::browseCli);
    connect(m_cliEdit, &QLineEdit::editingFinished, this, [this] {
        const QString p = m_cliEdit->text().trimmed();
        if (m_flasher) m_flasher->setCliPath(p);
        QSettings().setValue("cubeProgrammerPath", p);
    });
    cliRow->addWidget(m_cliEdit, 1);
    cliRow->addWidget(m_cliBrowse);
    v->addLayout(cliRow);

    return box;
}

void MainWindow::setFirmware(const QString &path)
{
    m_fwEdit->setText(path);
    if (!path.isEmpty())
        QSettings().setValue("lastFirmware", path);
}

bool MainWindow::firmwareValid() const
{
    const QString p = m_fwEdit->text();
    return !p.isEmpty() && QFileInfo::exists(p);
}

void MainWindow::browseFirmware()
{
    const QString last = m_fwEdit->text().isEmpty()
        ? QSettings().value("lastFirmware").toString() : m_fwEdit->text();
    const QString p = QFileDialog::getOpenFileName(
        this, tr("选择固件"), last, tr("固件 (*.bin *.hex *.elf *.dfu);;所有文件 (*.*)"));
    if (!p.isEmpty()) {
        setFirmware(p);
        maybeAutoFlash();
    }
}

void MainWindow::browseCli()
{
    const QString p = QFileDialog::getOpenFileName(
        this, tr("选择 STM32_Programmer_CLI.exe"), m_cliEdit->text(),
        tr("可执行文件 (*.exe);;所有文件 (*.*)"));
    if (!p.isEmpty()) {
        m_cliEdit->setText(p);
        if (m_flasher) m_flasher->setCliPath(p);
        QSettings().setValue("cubeProgrammerPath", p);
    }
}

void MainWindow::animateBar(QProgressBar *bar, QPropertyAnimation *anim, int target, int durMs)
{
    if (!bar || !anim) return;
    anim->stop();
    anim->setStartValue(bar->value());
    anim->setEndValue(qBound(0, target, 100));
    anim->setDuration(durMs);
    anim->setEasingCurve(QEasingCurve::Linear); // 匀速填充
    anim->start();
}

void MainWindow::onDfuPresentChanged(bool present)
{
    if (present) {
        m_dfuStatus->setText(tr("✔ 检测到 DFU 设备"));
        m_dfuStatus->setStyleSheet("color:#2ecc40;font-weight:bold;");
        // 新一次 DFU 插入: 清掉上次结果, 准备新一轮 (烧录中不清)
        if (!m_flashing)
            clearProgress();
        maybeAutoFlash();
    } else {
        m_dfuStatus->setText(tr("等待 DFU 设备…（请让飞控进入 DFU 模式并连接 USB）"));
        m_dfuStatus->setStyleSheet("color:#888;");
        m_flashedThisInsertion = false; // 拔出后允许再次自动烧录
        if (m_flashing) {
            // 烧录中 DFU 消失: 可能是末尾 -g 让飞控重启(正常), 也可能中途拔出。
            // 启动看门狗: 若 CLI 进程未在短时间内退出且未完成校验, 判定拔出并中止。
            startFlashWatchdog();
        }
        // 非烧录中: 不清空进度条, 保留上次成功结果
    }
    m_flashBtn->setEnabled(present && firmwareValid() && !m_flashing);
}

void MainWindow::startFlashWatchdog()
{
    if (m_flashWatchdog)
        m_flashWatchdog->start(3000);  // 3 秒: 成功路径进程会远早于此退出
}

void MainWindow::clearProgress()
{
    m_eraseAnim->stop();
    m_writeAnim->stop();
    m_eraseBar->setValue(0);
    m_writeBar->setValue(0);
    m_flashResult->setText(tr("空闲"));
    m_flashResult->setStyleSheet("");
}

void MainWindow::maybeAutoFlash()
{
    if (m_flashArm && m_flashArm->isChecked() && m_dfuWatcher && m_dfuWatcher->isPresent()
        && firmwareValid() && !m_flashing && !m_flashedThisInsertion)
        startFlash();
    if (m_flashBtn)
        m_flashBtn->setEnabled(m_dfuWatcher && m_dfuWatcher->isPresent()
                               && firmwareValid() && !m_flashing);
}

void MainWindow::startFlash()
{
    if (m_flashing) return;
    if (!firmwareValid()) {
        m_flashResult->setText(tr("请先选择有效的固件文件。"));
        m_flashResult->setStyleSheet("color:#ff851b;");
        return;
    }
    if (!m_dfuWatcher || !m_dfuWatcher->isPresent()) {
        m_flashResult->setText(tr("未检测到 DFU 设备。"));
        m_flashResult->setStyleSheet("color:#ff851b;");
        return;
    }
    m_flashedThisInsertion = true;
    m_eraseBar->setValue(0);
    m_writeBar->setValue(0);
    m_log->clear();

    // 按固件大小估计各阶段耗时 (用于平滑填充, ~28KB/s 写入)
    const qint64 sz = QFileInfo(m_fwEdit->text()).size();
    const int kb = int(sz / 1024) + 1;
    m_estEraseMs = qBound(1500, kb * 8,  15000);
    m_estWriteMs = qBound(3000, kb * 40, 90000);

    m_flasher->setFullErase(m_fullErase->isChecked());
    m_flasher->flash(m_fwEdit->text(), 0x08000000);
}

void MainWindow::onFlashStarted()
{
    m_flashing = true;
    m_writeAnimStarted = false;
    m_usbLost = false;
    m_flashTimer.start();               // 计时开始
    m_flashBtn->setEnabled(false);
    m_fwBrowse->setEnabled(false);
    m_flashResult->setText(tr("擦除中…"));
    m_flashResult->setStyleSheet("color:#0074d9;");

    // 时间线自驱动: CubeProgrammer 经管道会缓冲输出, 文字标记不实时到达, 因此进度条
    // 从烧录开始就按估计耗时匀速填充(到95%封顶), 真正完成时由标记吸附到100%。
    m_eraseBar->setValue(0);
    m_writeBar->setValue(0);
    animateBar(m_eraseBar, m_eraseAnim, 95, m_estEraseMs);
    QTimer::singleShot(m_estEraseMs, this, [this] { startWriteAnim(); });
}

void MainWindow::startWriteAnim()
{
    if (!m_flashing || m_writeAnimStarted) return;
    m_writeAnimStarted = true;
    m_eraseAnim->stop();
    m_eraseBar->setValue(100);
    m_flashResult->setText(tr("写入中…"));
    animateBar(m_writeBar, m_writeAnim, 95, m_estWriteMs);
}

void MainWindow::onFlashPhase(int phase)
{
    if (phase == DfuFlasher::PhaseWrite) {
        startWriteAnim();                 // 下载文字到达 -> 立即进入写入阶段
    } else if (phase == DfuFlasher::PhaseVerify) {
        m_writeAnim->stop();
        m_writeBar->setValue(100);
        m_flashResult->setText(tr("校验中…"));
    }
}

void MainWindow::onFlashProgress(int phase, int percent)
{
    if (percent >= 100 && phase == DfuFlasher::PhaseWrite) {
        m_writeAnim->stop();
        m_writeBar->setValue(100);
    }
}

void MainWindow::onFlashLog(const QString &line)
{
    if (m_log) m_log->appendPlainText(line);
    if (line.contains("Error", Qt::CaseInsensitive)) {
        m_flashResult->setText(line);
        m_flashResult->setStyleSheet("color:#ff4136;");
    }
}

void MainWindow::onFlashFinished(bool ok, const QString &message)
{
    if (m_flashWatchdog) m_flashWatchdog->stop();
    m_flashing = false;
    m_writeAnimStarted = false;
    m_eraseAnim->stop();
    m_writeAnim->stop();
    m_fwBrowse->setEnabled(true);

    // 看门狗判定的 USB 断开优先于 CLI 退出码
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
        const double secs = m_flashTimer.elapsed() / 1000.0;
        m_flashResult->setText(QString("✔ %1（耗时 %2 秒）").arg(finalMsg).arg(secs, 0, 'f', 1));
        m_flashResult->setStyleSheet("color:#2ecc40;font-weight:bold;");
    } else {
        m_flashResult->setText("✘ " + finalMsg);
        m_flashResult->setStyleSheet("color:#ff4136;font-weight:bold;");
    }
    m_flashBtn->setEnabled(m_dfuWatcher && m_dfuWatcher->isPresent() && firmwareValid());
}
