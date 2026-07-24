#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>
#include <QHash>

QT_BEGIN_NAMESPACE
class QPushButton;
class QLabel;
class QLineEdit;
class QCheckBox;
class QProgressBar;
class QPlainTextEdit;
class QPropertyAnimation;
class QTimer;
QT_END_NAMESPACE

class DfuWatcher;
class DfuFlasher;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void browseFirmware();
    void browseCli();
    void onDfuPresentChanged(bool present);
    void startFlash();
    void onFlashStarted();
    void onFlashPhase(int phase);
    void onFlashProgress(int phase, int percent);
    void onFlashLog(const QString &line);
    void onFlashFinished(bool ok, const QString &message);

private:
    QWidget *buildPanel();
    void setFirmware(const QString &path);
    bool firmwareValid() const;
    void maybeAutoFlash();
    void animateBar(QProgressBar *bar, QPropertyAnimation *anim, int target, int durMs);
    void startWriteAnim();
    void clearProgress();        // 进度条归零 + 结果回空闲
    void startFlashWatchdog();   // 烧录中 DFU 消失: 看门狗, 卡住则判拔出并中止

    DfuWatcher *m_dfuWatcher = nullptr;
    DfuFlasher *m_flasher = nullptr;
    QLineEdit   *m_fwEdit = nullptr;
    QPushButton *m_fwBrowse = nullptr;
    QLabel      *m_dfuStatus = nullptr;
    QCheckBox   *m_flashArm = nullptr;
    QCheckBox   *m_fullErase = nullptr;
    QPushButton *m_flashBtn = nullptr;
    QProgressBar *m_eraseBar = nullptr;
    QProgressBar *m_writeBar = nullptr;
    QLabel      *m_flashResult = nullptr;
    QLineEdit   *m_cliEdit = nullptr;
    QPushButton *m_cliBrowse = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_logToggle = nullptr;
    QPropertyAnimation *m_eraseAnim = nullptr;
    QPropertyAnimation *m_writeAnim = nullptr;
    QTimer *m_flashWatchdog = nullptr;  // 烧录中 DFU 消失后: 看门狗, 卡住则中止
    bool m_flashing = false;
    bool m_flashedThisInsertion = false;
    bool m_writeAnimStarted = false;
    bool m_usbLost = false;            // 看门狗判定 USB 断开
    QElapsedTimer m_flashTimer;   // 烧录耗时
    int m_estEraseMs = 3000;   // 擦除阶段估计耗时 (平滑填充用)
    int m_estWriteMs = 15000;  // 写入阶段估计耗时
};

#endif // MAINWINDOW_H
