#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("SequoiaFlasher");   // QSettings 路径稳定标识
    app.setApplicationDisplayName(QString::fromUtf8("STM32固件烧录"));
    app.setOrganizationName("SequoiaRC");

    MainWindow w;
    w.show();
    return app.exec();
}
