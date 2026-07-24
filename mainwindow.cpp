#include "mainwindow.h"
#include "deviceslot.h"
#include "flash/dfudevlist.h"
#include "flash/dfuflasher.h"

#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QFileInfo>
#include <QFileDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("STM32固件烧录"));

    QWidget *central = new QWidget;
    QVBoxLayout *root = new QVBoxLayout(central);
    root->addWidget(buildTopBar());

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    QWidget *host = new QWidget;
    m_slotsLayout = new QVBoxLayout(host);
    m_slotsLayout->setContentsMargins(4, 4, 4, 4);
    m_slotsLayout->addStretch(1);
    m_scroll->setWidget(host);
    root->addWidget(m_scroll, 1);

    setCentralWidget(central);
    resize(720, 640);

    m_devList = new DfuDeviceList(this);
    connect(m_devList, &DfuDeviceList::devicesChanged, this, &MainWindow::onDevicesChanged);

    setFirmware(QSettings().value("lastFirmware").toString());
    // CLI 自动探测 + 持久化
    DfuFlasher probe;
    QString cliPath = QSettings().value("cubeProgrammerPath").toString();
    if (cliPath.isEmpty()) cliPath = probe.cliPath();
    m_cliEdit->setText(cliPath);

    m_devList->start();
}

QWidget *MainWindow::buildTopBar()
{
    QWidget *bar = new QWidget;
    QVBoxLayout *v = new QVBoxLayout(bar);
    v->setContentsMargins(0, 0, 0, 0);

    // 固件
    QHBoxLayout *fwRow = new QHBoxLayout;
    fwRow->addWidget(new QLabel(tr("固件:")));
    m_fwEdit = new QLineEdit;
    m_fwEdit->setReadOnly(true);
    m_fwBrowse = new QPushButton(tr("浏览…"));
    connect(m_fwBrowse, &QPushButton::clicked, this, &MainWindow::browseFirmware);
    fwRow->addWidget(m_fwEdit, 1);
    fwRow->addWidget(m_fwBrowse);
    v->addLayout(fwRow);

    // 控制行
    QHBoxLayout *ctlRow = new QHBoxLayout;
    m_flashArm = new QCheckBox(tr("检测到即自动烧录"));
    m_flashArm->setChecked(QSettings().value("flashAutoArm", true).toBool());
    connect(m_flashArm, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("flashAutoArm", on);
    });
    m_fullErase = new QCheckBox(tr("全盘擦除"));
    m_fullErase->setToolTip(tr("开启则在写入前擦除整片 Flash (默认关闭)"));
    m_fullErase->setChecked(QSettings().value("fullErase", false).toBool());
    connect(m_fullErase, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue("fullErase", on);
        applySharedToSlots();
    });
    m_flashAllBtn = new QPushButton(tr("全部烧录"));
    connect(m_flashAllBtn, &QPushButton::clicked, this, &MainWindow::flashAll);
    m_clearBtn = new QPushButton(tr("清除已完成"));
    m_clearBtn->setToolTip(tr("移除已烧录完成(成功/失败)的槽位"));
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::clearFinished);
    m_countLabel = new QLabel;
    ctlRow->addWidget(m_countLabel, 1);
    ctlRow->addWidget(m_flashArm);
    ctlRow->addWidget(m_fullErase);
    ctlRow->addWidget(m_flashAllBtn);
    ctlRow->addWidget(m_clearBtn);
    v->addLayout(ctlRow);

    // CLI 路径
    QHBoxLayout *cliRow = new QHBoxLayout;
    cliRow->addWidget(new QLabel("CLI:"));
    m_cliEdit = new QLineEdit;
    m_cliEdit->setPlaceholderText(tr("STM32_Programmer_CLI.exe 路径"));
    m_cliBrowse = new QPushButton(tr("…"));
    m_cliBrowse->setMaximumWidth(32);
    connect(m_cliBrowse, &QPushButton::clicked, this, &MainWindow::browseCli);
    connect(m_cliEdit, &QLineEdit::editingFinished, this, [this] {
        const QString p = m_cliEdit->text().trimmed();
        QSettings().setValue("cubeProgrammerPath", p);
        applySharedToSlots();
    });
    cliRow->addWidget(m_cliEdit, 1);
    cliRow->addWidget(m_cliBrowse);
    v->addLayout(cliRow);

    return bar;
}

bool MainWindow::firmwareValid() const
{
    const QString p = m_fwEdit->text();
    return !p.isEmpty() && QFileInfo::exists(p);
}

void MainWindow::setFirmware(const QString &path)
{
    m_fwEdit->setText(path);
    if (!path.isEmpty())
        QSettings().setValue("lastFirmware", path);
    applySharedToSlots();
}

void MainWindow::browseFirmware()
{
    const QString last = m_fwEdit->text().isEmpty()
        ? QSettings().value("lastFirmware").toString() : m_fwEdit->text();
    const QString p = QFileDialog::getOpenFileName(
        this, tr("选择固件"), last, tr("固件 (*.bin *.hex *.elf *.dfu);;所有文件 (*.*)"));
    if (!p.isEmpty())
        setFirmware(p);
}

void MainWindow::browseCli()
{
    const QString p = QFileDialog::getOpenFileName(
        this, tr("选择 STM32_Programmer_CLI.exe"), m_cliEdit->text(),
        tr("可执行文件 (*.exe);;所有文件 (*.*)"));
    if (!p.isEmpty()) {
        m_cliEdit->setText(p);
        QSettings().setValue("cubeProgrammerPath", p);
        applySharedToSlots();
    }
}

DeviceSlot *MainWindow::findSlot(const QString &usbN)
{
    for (DeviceSlot *s : m_slots)
        if (s->port() == usbN) return s;
    return nullptr;
}

void MainWindow::applySharedToSlots()
{
    const QString fw = m_fwEdit->text();
    const QString cli = m_cliEdit->text().trimmed();
    const bool fe = m_fullErase->isChecked();
    for (DeviceSlot *s : m_slots) {
        s->setFirmwarePath(fw);
        s->setCliPath(cli);
        s->setFullErase(fe);
    }
}

void MainWindow::onDevicesChanged(const QList<DfuDevice> &devs)
{
    syncSlots(devs);
    m_countLabel->setText(tr("检测到 %1 个 DFU 设备").arg(devs.size()));
    m_flashAllBtn->setEnabled(firmwareValid() && !devs.isEmpty());
}

void MainWindow::syncSlots(const QList<DfuDevice> &devs)
{
    // 1) 更新/新增: 按 usbN 匹配 (USB 序列号是通用的 STM32FxSTM32, 不能区分板子)
    QSet<QString> present;
    for (const DfuDevice &d : devs) {
        present.insert(d.usbN);
        DeviceSlot *s = findSlot(d.usbN);
        if (s) {
            if (s->awaitingReinsert()) {
                // 该 port 上次烧完已离开, 现在又有设备: 复位并重烧
                s->prepareReflash();
                s->setDevice(d);
                if (m_flashArm->isChecked() && firmwareValid())
                    s->startFlash();
            } else {
                s->setDevice(d);          // 更新 SN 显示
            }
        } else {
            s = new DeviceSlot(d, this);
            s->setFirmwarePath(m_fwEdit->text());
            s->setCliPath(m_cliEdit->text().trimmed());
            s->setFullErase(m_fullErase->isChecked());
            m_slotsLayout->insertWidget(m_slotsLayout->count() - 1, s); // 插在 stretch 前
            m_slots.append(s);
            // 自动烧录: 新设备出现即开烧
            if (m_flashArm->isChecked() && firmwareValid())
                s->startFlash();
        }
    }
    // 2) 移除/标记: port 不再在线
    for (int i = m_slots.size() - 1; i >= 0; --i) {
        DeviceSlot *s = m_slots[i];
        if (present.contains(s->port()))
            continue;                 // 仍在线: 保留
        if (s->isFlashing()) {
            s->onDeviceGone();        // 烧录中消失: 看门狗接管, 等其结束
            continue;
        }
        if (s->isFinished()) {
            s->markDeviceLeft();      // 已完成且设备离开: 保留结果, 标记等待重插重烧
            continue;
        }
        // 空闲且未烧过: 设备拔了, 移除
        m_slotsLayout->removeWidget(s);
        m_slots.removeAt(i);
        delete s;
    }
}

void MainWindow::flashAll()
{
    if (!firmwareValid()) return;
    for (DeviceSlot *s : m_slots)
        s->startFlash();
}

void MainWindow::clearFinished()
{
    for (int i = m_slots.size() - 1; i >= 0; --i) {
        DeviceSlot *s = m_slots[i];
        if (s->isFinished() && !s->isFlashing()) {
            m_slotsLayout->removeWidget(s);
            m_slots.removeAt(i);
            delete s;
        }
    }
}
