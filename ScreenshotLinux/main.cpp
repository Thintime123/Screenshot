#include <QApplication>
#include <QScreen>
#include "screenshotwindow.h"

int main(int argc, char *argv[])
{
    // 设置应用程序属性
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication app(argc, argv);
    
    // 设置应用程序信息
    app.setApplicationName("ScreenshotLinux");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("ScreenshotLinux");
    app.setOrganizationDomain("screenshot.linux.local");
    
    // 创建截图窗口（将以托盘图标形式运行）
    ScreenshotWindow window;
    
    return app.exec();
}