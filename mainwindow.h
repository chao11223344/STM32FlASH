#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include "flash/dfudevlist.h"

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
class QVBoxLayout;
class QScrollArea;
QT_END_NAMESPACE

class DeviceSlot;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void browseFirmware();
    void browseCli();
    void onDevicesChanged(const QList<DfuDevice> &devs);
    void flashAll();
    void clearFinished();

private:
    QWidget *buildTopBar();
    void syncSlots(const QList<DfuDevice> &devs);
    DeviceSlot *findSlot(const QString &sn);
    void applySharedToSlots();
    bool firmwareValid() const;
    void setFirmware(const QString &path);

    DfuDeviceList *m_devList = nullptr;

    // 共享顶栏
    QLineEdit   *m_fwEdit = nullptr;
    QPushButton *m_fwBrowse = nullptr;
    QLineEdit   *m_cliEdit = nullptr;
    QPushButton *m_cliBrowse = nullptr;
    QCheckBox   *m_fullErase = nullptr;
    QCheckBox   *m_flashArm = nullptr;
    QPushButton *m_flashAllBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QLabel      *m_countLabel = nullptr;

    // 槽位列表
    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_slotsLayout = nullptr;
    QList<DeviceSlot*> m_slots;
};

#endif // MAINWINDOW_H
