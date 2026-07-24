#include "dfuwatcher.h"

#include <QTimer>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>

static bool scanWin()
{
    // 枚举所有在线的 USB 总线设备, 检查硬件 ID 是否含 VID_0483&PID_DF11
    HDEVINFO h = SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                      DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    const QString target = QStringLiteral("VID_%1&PID_%2")
        .arg(DfuWatcher::DFU_VID, 4, 16, QChar('0'))
        .arg(DfuWatcher::DFU_PID, 4, 16, QChar('0')); // "VID_0483&PID_df11"

    SP_DEVINFO_DATA d;
    d.cbSize = sizeof(d);
    bool found = false;
    wchar_t buf[1024];

    for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &d); ++i) {
        DWORD type = 0, size = 0;
        if (SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_HARDWAREID, &type,
                                              reinterpret_cast<PBYTE>(buf),
                                              sizeof(buf), &size)) {
            const QString s = QString::fromWCharArray(buf, int(size / sizeof(wchar_t)));
            if (s.contains(target, Qt::CaseInsensitive)) {
                found = true;
                break;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(h);
    return found;
}
#endif // Q_OS_WIN

DfuWatcher::DfuWatcher(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &DfuWatcher::poll);
}

bool DfuWatcher::scanPresent()
{
#ifdef Q_OS_WIN
    return scanWin();
#else
    return false;
#endif
}

void DfuWatcher::start(int intervalMs)
{
    m_present = scanPresent();
    emit presentChanged(m_present);
    m_timer->start(intervalMs);
}

void DfuWatcher::stop()
{
    m_timer->stop();
}

void DfuWatcher::poll()
{
    const bool p = scanPresent();
    if (p != m_present) {
        m_present = p;
        emit presentChanged(p);
    }
}
