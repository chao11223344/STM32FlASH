#include "dfudevlist.h"

#include <QTimer>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>

static QList<DfuDevice> scanWin()
{
    QList<DfuDevice> list;
    HDEVINFO h = SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                      DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (h == INVALID_HANDLE_VALUE)
        return list;

    const QString target = QStringLiteral("VID_%1&PID_%2")
        .arg(DfuDeviceList::DFU_VID, 4, 16, QChar('0'))
        .arg(DfuDeviceList::DFU_PID, 4, 16, QChar('0')); // "VID_0483&PID_df11"

    SP_DEVINFO_DATA d;
    d.cbSize = sizeof(d);
    wchar_t buf[1024];
    for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &d); ++i) {
        DWORD type = 0, size = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_HARDWAREID, &type,
                                               reinterpret_cast<PBYTE>(buf),
                                               sizeof(buf), &size))
            continue;
        const QString s = QString::fromWCharArray(buf, int(size / sizeof(wchar_t)));
        if (!s.contains(target, Qt::CaseInsensitive))
            continue;
        // 硬件 ID 形如 USB\VID_0483&PID_DF11\<SN>, 取末段作 SN
        DfuDevice dev;
        dev.sn = s.section(QLatin1Char('\\'), -1).trimmed();
        if (dev.sn.isEmpty())
            dev.sn = QStringLiteral("(未知SN)");
        list.append(dev);
    }
    SetupDiDestroyDeviceInfoList(h);

    // 按枚举顺序赋予 usbN (1-based), 与 CubeProgrammer 的 -c port=usbN 对应
    for (int i = 0; i < list.size(); ++i)
        list[i].usbN = QStringLiteral("usb%1").arg(i + 1);
    return list;
}
#endif // Q_OS_WIN

QList<DfuDevice> DfuDeviceList::scan()
{
#ifdef Q_OS_WIN
    return scanWin();
#else
    return {};
#endif
}

DfuDeviceList::DfuDeviceList(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &DfuDeviceList::poll);
}

void DfuDeviceList::start(int intervalMs)
{
    m_devices = scan();
    emit devicesChanged(m_devices);
    m_timer->start(intervalMs);
}

void DfuDeviceList::stop()
{
    m_timer->stop();
}

void DfuDeviceList::poll()
{
    if (m_paused)
        return;
    const QList<DfuDevice> cur = scan();
    // 变化判定: 数量或 SN 集合不同
    bool changed = cur.size() != m_devices.size();
    if (!changed) {
        for (int i = 0; i < cur.size(); ++i) {
            if (cur[i].sn != m_devices[i].sn) { changed = true; break; }
        }
    }
    if (changed) {
        m_devices = cur;
        emit devicesChanged(m_devices);
    }
}
