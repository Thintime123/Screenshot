#include <QApplication>
#include <QScreen>
#include <QDebug>
#include <QTimer>
#include <QLoggingCategory>
#include "screenshotwindow.h"

int main(int argc, char *argv[])
{
    // 启用更详细的调试输出
    QLoggingCategory::setFilterRules("*.debug=true");
    qSetMessagePattern("[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{type}] %{message}");
    
    // 设置Qt应用在Wayland下的特定环境变量
    qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
    
    // 设置应用程序属性
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication app(argc, argv);
    
    // 设置应用程序信息
    app.setApplicationName("ScreenshotLinux");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("ScreenshotLinux");
    app.setOrganizationDomain("screenshot.linux.local");
    
    qDebug() << "应用程序启动";
    qDebug() << "平台:" << QApplication::platformName();
    qDebug() << "是否Wayland?" << QApplication::platformName().contains("wayland", Qt::CaseInsensitive);
    
    // 创建截图窗口（将以托盘图标形式运行）
    ScreenshotWindow *window = new ScreenshotWindow();
    
    // 使用计时器延迟初始化托盘图标，避免Wayland环境下的可能问题
    QTimer::singleShot(500, [window]() {
        qDebug() << "初始化托盘图标";
        window->setupTrayIcon();
    });
    
    // 安装事件过滤器以确保应用不会意外退出
    class GlobalEventFilter : public QObject {
    protected:
        bool eventFilter(QObject *obj, QEvent *event) override {
            if (event->type() == QEvent::Quit) {
                qDebug() << "捕获到退出事件，阻止默认处理";
                return true; // 阻止应用退出
            }
            return QObject::eventFilter(obj, event);
        }
    };
    
    GlobalEventFilter *filter = new GlobalEventFilter();
    app.installEventFilter(filter);
    
    return app.exec();
}