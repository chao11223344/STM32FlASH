#-------------------------------------------------
# SequoiaRC 固件烧录 (独立工具)
# 仅做飞控 STM32 经 USB DFU 烧录固件, 逻辑与 upper_computer 中的烧录模块一致。
#-------------------------------------------------

QT += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

TARGET   = stm32_flasher
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    flash/dfuwatcher.cpp \
    flash/dfuflasher.cpp

HEADERS += \
    mainwindow.h \
    flash/dfuwatcher.h \
    flash/dfuflasher.h

# Windows: DFU 设备检测需要 SetupAPI
win32: LIBS += -lsetupapi

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
