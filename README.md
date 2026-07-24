# STM32固件烧录 (独立工具)

仅做飞控 STM32 经 **USB DFU** 烧录固件的独立 Qt Widgets 工具。烧录逻辑与
`../upper_computer` 中集成的烧录面板完全一致（共用同一份 `flash/` 模块代码）。

## 功能

- 「浏览…」选择固件（`.bin/.hex/.elf/.dfu`；`.bin` 默认烧到 `0x08000000`）。
- 让飞控进入 **DFU 模式** 并连接 USB（DFU 设备 VID `0x0483` / PID `0xDF11`）。
- 后台用 SetupAPI 轮询检测；勾选「检测到 DFU 即自动烧录」后**插上即烧**
  （同一次插入只烧一次，拔出再插可再烧），也可点「立即烧录」。
- **擦除 / 写入两条进度条**（时间线自驱动匀速填充，因 CubeProgrammer 经管道缓冲的百分比不可靠）。
- 可折叠日志框，实时显示烧录过程（已去乱码）。
- CubeProgrammer CLI 路径自动探测 + 可手动指定，路径与设置持久化。

## 工程结构

```
firmware_flasher/
├─ firmware_flasher.pro    # qmake 工程
├─ main.cpp
├─ mainwindow.{h,cpp}      # 烧录面板 (整个软件即此窗口)
└─ flash/
   ├─ dfuwatcher.{h,cpp}   # SetupAPI 轮询检测 DFU 设备 (VID_0483&PID_DF11)
   └─ dfuflasher.{h,cpp}   # 调 STM32CubeProgrammer CLI 经 USB DFU 烧录(分擦除/写入/校验阶段)
```

## 构建 (Windows / mingw)

QtCreator 打开 `firmware_flasher.pro`，选 **Desktop Qt 5.12.6 MinGW 64-bit** 套件，构建运行。

命令行：

```sh
export PATH="/d/Qt/Qt5.12.6/5.12.6/mingw73_64/bin:/d/Qt/Qt5.12.6/Tools/mingw730_64/bin:$PATH"
qmake firmware_flasher.pro
mingw32-make -j
```

## 依赖

- **STM32CubeProgrammer**（提供 `STM32_Programmer_CLI.exe` 及 DFU 驱动，免 Zadig）。
  程序会自动探测 `C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/`，
  找不到时在界面手动指定。

## 与上位机的关系

本工具是 `../upper_computer` 烧录功能的独立抽取，`flash/` 模块代码相同。
若改了烧录逻辑，请两边同步（直接覆盖 `flash/` 下四个文件即可）。
