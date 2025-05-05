#include "screenshotwindow.h"
#include <QApplication>
#include <QScreen>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QInputDialog>
#include <QToolButton>
#include <QVBoxLayout>
#include <QGraphicsBlurEffect>
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QCloseEvent>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QEventLoop>
#include <QBuffer>
#include <QMimeData>
#include <QProcess>
#include <QImageWriter>
#include <QWindow>
#include <QRandomGenerator>
#include <QRegularExpression> // 添加正则表达式支持

ScreenshotWindow::ScreenshotWindow(QWidget *parent)
    : QWidget(parent)
    , m_isSelecting(false)
    , m_hasSelected(false)
    , m_isScreenshotMode(false)
    , m_currentMode(DrawMode::None)
    , m_rubberBand(new QRubberBand(QRubberBand::Rectangle, this))
    , m_toolBar(new QToolBar(this))
    , m_trayIcon(nullptr)
    , m_trayIconMenu(nullptr)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    
    m_toolBar->setVisible(false);
    m_toolBar->setFixedHeight(40);
    
    m_rectAction = m_toolBar->addAction("矩形");
    m_circleAction = m_toolBar->addAction("圆形");
    m_arrowAction = m_toolBar->addAction("箭头");
    m_textAction = m_toolBar->addAction("文字");
    m_brushAction = m_toolBar->addAction("画笔");
    m_mosaicAction = m_toolBar->addAction("马赛克");
    m_undoAction = m_toolBar->addAction("撤销");
    m_saveAction = m_toolBar->addAction("保存");
    m_cancelAction = m_toolBar->addAction("取消");
    m_finishAction = m_toolBar->addAction("完成");
    
    connect(m_rectAction, &QAction::triggered, this, &ScreenshotWindow::drawRectangle);
    connect(m_circleAction, &QAction::triggered, this, &ScreenshotWindow::drawCircle);
    connect(m_arrowAction, &QAction::triggered, this, &ScreenshotWindow::drawArrow);
    connect(m_textAction, &QAction::triggered, this, &ScreenshotWindow::drawText);
    connect(m_brushAction, &QAction::triggered, this, &ScreenshotWindow::drawBrush);
    connect(m_undoAction, &QAction::triggered, this, &ScreenshotWindow::undo);
    connect(m_saveAction, &QAction::triggered, this, &ScreenshotWindow::saveScreenshot);
    connect(m_cancelAction, &QAction::triggered, this, &ScreenshotWindow::cancelScreenshot);
    connect(m_finishAction, &QAction::triggered, this, &ScreenshotWindow::finishScreenshot);
    
    // 不在构造函数中初始化托盘图标，而是由main.cpp调用
    // setupTrayIcon();
    
    hide();
}

ScreenshotWindow::~ScreenshotWindow()
{
    delete m_rubberBand;
    delete m_trayIcon;
    delete m_trayIconMenu;
}

void ScreenshotWindow::setupTrayIcon()
{
    // 首先检查当前环境
    bool isWayland = QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive);
    qDebug() << "设置托盘图标, 平台:" << QGuiApplication::platformName() 
             << (isWayland ? "(Wayland)" : "(X11/其他)");
    
    // 创建托盘菜单
    m_trayIconMenu = new QMenu(this);
    
    m_screenshotAction = new QAction("开始截图", this);
    // 在Wayland环境下, 使用一个lambda中间层来调用startScreenshot，以避免直接调用
    if(isWayland) {
        connect(m_screenshotAction, &QAction::triggered, this, [this]() {
            qDebug() << "通过菜单项触发截图(Wayland安全方式)";
            QTimer::singleShot(100, this, &ScreenshotWindow::startScreenshot);
        });
    } else {
        connect(m_screenshotAction, &QAction::triggered, this, &ScreenshotWindow::startScreenshot);
    }
    m_trayIconMenu->addAction(m_screenshotAction);
    
    m_aboutAction = new QAction("关于", this);
    connect(m_aboutAction, &QAction::triggered, this, &ScreenshotWindow::showAboutDialog);
    m_trayIconMenu->addAction(m_aboutAction);
    
    m_trayIconMenu->addSeparator();
    
    m_quitAction = new QAction("退出", this);
    // 在Wayland环境下, 退出也需要特殊处理
    if(isWayland) {
        connect(m_quitAction, &QAction::triggered, this, [this]() {
            qDebug() << "通过菜单项退出(Wayland安全方式)";
            QTimer::singleShot(200, this, &ScreenshotWindow::quitApplication);
        });
    } else {
        connect(m_quitAction, &QAction::triggered, this, &ScreenshotWindow::quitApplication);
    }
    m_trayIconMenu->addAction(m_quitAction);
    
    // 创建托盘图标
    m_trayIcon = new QSystemTrayIcon(this);
    
    // 设置图标
    QIcon icon = QIcon::fromTheme("camera-photo");
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip("截图工具");
    
    // 设置托盘菜单
    m_trayIcon->setContextMenu(m_trayIconMenu);
    
    // 将托盘信号连接到一个安全的中间处理函数
    if(isWayland) {
        disconnect(m_trayIcon, &QSystemTrayIcon::activated, this, &ScreenshotWindow::trayIconActivated);
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, &ScreenshotWindow::safeTrayIconActivated);
    } else {
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, &ScreenshotWindow::trayIconActivated);
    }
    
    // 确保图标可见
    m_trayIcon->show();
    qDebug() << "托盘图标设置完成, 可见性:" << m_trayIcon->isVisible();
}

void ScreenshotWindow::safeTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    qDebug() << "Wayland安全模式：托盘图标被激活，原因:" << static_cast<int>(reason);
    
    // 使用全异步方式处理托盘事件，避免Wayland环境下的崩溃
    if (reason == QSystemTrayIcon::Trigger) {
        qDebug() << "Wayland安全模式：触发截图操作（延迟执行）";
        
        // 关键：使用QueuedConnection而不是直接调用或单纯的延时
        QMetaObject::invokeMethod(this, [this]() {
            qDebug() << "Wayland安全模式：开始执行异步截图操作";
            
            // 二次确认程序仍在运行
            if (QCoreApplication::instance() && !QCoreApplication::closingDown()) {
                QTimer::singleShot(200, this, [this]() {
                    qDebug() << "Wayland安全模式：实际执行截图操作";
                    startScreenshot();
                });
            } else {
                qDebug() << "Wayland安全模式：应用程序正在关闭，取消操作";
            }
        }, Qt::QueuedConnection);
    } else if (reason == QSystemTrayIcon::Context) {
        qDebug() << "Wayland安全模式：右键菜单被触发";
        // 右键菜单由Qt自动处理
    } else {
        qDebug() << "Wayland安全模式：其他类型的托盘激活:" << static_cast<int>(reason);
    }
}

void ScreenshotWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        startScreenshot();
    }
}

void ScreenshotWindow::startScreenshot()
{
    qDebug() << "开始截图操作";
    m_isScreenshotMode = true;
    m_isSelecting = false;
    m_hasSelected = false;
    m_drawItems.clear();
    m_undoItems.clear();
    m_currentMode = DrawMode::None;
    
    // 在Wayland环境下额外处理
    bool isWayland = QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive);
    if (isWayland) {
        qDebug() << "Wayland环境检测到，使用特殊处理";
        
        // 先隐藏所有可能干扰的UI元素
        if (m_toolBar) m_toolBar->hide();
        if (m_trayIcon) m_trayIcon->hide();
        
        // 确保窗口不会影响屏幕捕获
        hide();
        
        // 给系统一些时间来处理窗口隐藏
        QTimer::singleShot(300, this, [this]() {
            grabScreen();
            
            // 在屏幕截图完成后显示窗口
            if (!m_screenPixmap.isNull()) {
                showFullScreen();
                
                // 确保窗口覆盖整个屏幕
                QScreen *screen = QGuiApplication::primaryScreen();
                if (screen) {
                    QRect screenGeom = screen->geometry();
                    setGeometry(screenGeom);
                    
                    qDebug() << "设置窗口几何形状:" << screenGeom;
                    qDebug() << "当前窗口几何形状:" << geometry();
                    qDebug() << "截图大小:" << m_screenPixmap.size();
                    
                    // 重要：确保截图大小与屏幕匹配
                    if (m_screenPixmap.size() != screenGeom.size()) {
                        qDebug() << "调整截图大小以匹配屏幕";
                        
                        QPixmap scaledPixmap;
                        // 如果截图比屏幕小，则放大
                        if (m_screenPixmap.width() < screenGeom.width() || 
                            m_screenPixmap.height() < screenGeom.height()) {
                            scaledPixmap = m_screenPixmap.scaled(
                                screenGeom.size(),
                                Qt::KeepAspectRatio,
                                Qt::SmoothTransformation);
                        } else {
                            // 如果截图过大，则确保它适合屏幕
                            scaledPixmap = m_screenPixmap.scaled(
                                screenGeom.size(),
                                Qt::KeepAspectRatio,
                                Qt::SmoothTransformation);
                        }
                        
                        // 只在有效情况下更新截图
                        if (!scaledPixmap.isNull()) {
                            m_screenPixmap = scaledPixmap;
                        }
                    }
                }
            } else {
                QMessageBox::critical(nullptr, "截图失败", "无法捕获屏幕");
            }
        });
    } else {
        // 非Wayland环境使用原有方法
        grabScreen();
        showFullScreen();
    }
}

void ScreenshotWindow::grabScreen()
{
    // 清除之前的截图数据
    m_screenPixmap = QPixmap();
    
    // 获取屏幕截图的更可靠方法
    bool captureSuccess = false;
    
    // 检测当前显示服务器环境
    bool isWayland = QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive);
    qDebug() << "当前平台:" << QGuiApplication::platformName() << (isWayland ? "(Wayland)" : "(可能是X11)");
    
    // 在Wayland环境下，优先使用外部工具进行截图，避免放大问题
    if (isWayland) {
        // 尝试使用特定的Wayland屏幕捕获工具
        QString tempFile = QDir::tempPath() + "/screenshot_" + 
                          QString::number(QRandomGenerator::global()->generate()) + ".png";
        
        // 确保临时文件路径没有非ASCII字符
        if (tempFile.contains(QRegularExpression("[^\\x00-\\x7F]"))) {
            tempFile = "/tmp/screenshot_" + 
                      QString::number(QRandomGenerator::global()->generate()) + ".png";
        }
        
        qDebug() << "使用临时文件:" << tempFile;
        
        // 特别优先使用适合Wayland的工具
        QStringList waylandCmds;
        
        if (QFile::exists("/usr/bin/grim")) {
            waylandCmds << "/usr/bin/grim \"" + tempFile + "\"";
        }
        
        if (QFile::exists("/usr/bin/spectacle")) {
            waylandCmds << "/usr/bin/spectacle -b -n -o \"" + tempFile + "\"";
        }
        
        for (const QString &cmd : waylandCmds) {
            if (captureSuccess) break;
            
            qDebug() << "尝试使用Wayland专用命令捕获屏幕:" << cmd;
            
            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            
            try {
                process.start("bash", QStringList() << "-c" << cmd);
                
                if (process.waitForFinished(5000)) {
                    if (process.exitCode() == 0) {
                        QFile file(tempFile);
                        if (file.exists() && file.size() > 0) {
                            qDebug() << "临时文件创建成功，大小:" << file.size() << "字节";
                            
                            // 加载临时文件
                            QImage capturedImage(tempFile);
                            if (!capturedImage.isNull()) {
                                m_screenPixmap = QPixmap::fromImage(capturedImage);
                                qDebug() << "使用Wayland专用工具" << cmd << "捕获屏幕成功";
                                captureSuccess = true;
                                
                                // 为安全起见，删除临时文件
                                QFile::remove(tempFile);
                                break;
                            }
                        }
                    } else {
                        qDebug() << "命令执行失败，退出码:" << process.exitCode();
                        qDebug() << "错误输出:" << process.readAllStandardError();
                    }
                } else {
                    qDebug() << "命令执行超时";
                }
            } catch (...) {
                qDebug() << "执行Wayland命令时捕获到异常";
            }
        }
        
        // 如果Wayland专用工具失败，尝试通过XDG-Desktop-Portal截图
        if (!captureSuccess) {
            qDebug() << "尝试使用XDG-Desktop-Portal方法";
            
            // 使用通用XDG-Portal的截图API（适用于大多数现代桌面环境）
            QProcess process;
            QString scriptPath = QDir::tempPath() + "/xdg_screenshot_" + 
                               QString::number(QRandomGenerator::global()->generate()) + ".sh";
            
            QFile script(scriptPath);
            if (script.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&script);
                out << "#!/bin/bash\n";
                // 使用xdg-desktop-portal提供的截图服务
                out << "dbus-send --session --print-reply --dest=org.freedesktop.portal.Desktop "
                       "/org/freedesktop/portal/desktop org.freedesktop.portal.Screenshot.Screenshot "
                       "boolean:true string:\"" << tempFile << "\" > /dev/null 2>&1\n";
                out << "sleep 2\n";  // 给操作系统时间保存截图
                script.close();
                
                // 使脚本可执行
                QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
                
                process.start("bash", QStringList() << scriptPath);
                if (process.waitForFinished(10000)) {  // 等待10秒
                    // 检查文件是否创建
                    QFile file(tempFile);
                    if (file.exists() && file.size() > 0) {
                        QImage capturedImage(tempFile);
                        if (!capturedImage.isNull()) {
                            m_screenPixmap = QPixmap::fromImage(capturedImage);
                            captureSuccess = true;
                            qDebug() << "使用XDG-Desktop-Portal捕获屏幕成功";
                        }
                    }
                }
                
                // 清理
                QFile::remove(scriptPath);
                QFile::remove(tempFile);
            }
        }
    }
    
    // 如果上述方法都失败，或者不是Wayland环境，尝试使用其他工具
    if (!captureSuccess) {
        qDebug() << "尝试使用备用截图方法";
        
        QString tempFile = QDir::tempPath() + "/screenshot_" + 
                      QString::number(QRandomGenerator::global()->generate()) + ".png";
        
        QStringList possibleCmds;
        
        // 检查可用的屏幕捕获工具
        if (QFile::exists("/usr/bin/gnome-screenshot")) {
            possibleCmds << "/usr/bin/gnome-screenshot -f \"" + tempFile + "\"";
        }
        
        if (QFile::exists("/usr/bin/ksnip")) {
            possibleCmds << "/usr/bin/ksnip -f \"" + tempFile + "\"";
        }
        
        if (QFile::exists("/usr/bin/spectacle")) {
            possibleCmds << "/usr/bin/spectacle -b -n -o \"" + tempFile + "\"";
        }
        
        if (QFile::exists("/usr/bin/scrot")) {
            possibleCmds << "/usr/bin/scrot \"" + tempFile + "\"";
        }
        
        if (QFile::exists("/usr/bin/maim")) {
            possibleCmds << "/usr/bin/maim \"" + tempFile + "\"";
        }
        
        if (QFile::exists("/usr/bin/import")) {
            possibleCmds << "/usr/bin/import -window root \"" + tempFile + "\"";
        }
        
        for (const QString &cmd : possibleCmds) {
            if (captureSuccess) break;
            
            qDebug() << "尝试使用外部命令捕获屏幕:" << cmd;
            
            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            
            try {
                process.start("bash", QStringList() << "-c" << cmd);
                
                if (process.waitForFinished(5000)) {
                    if (process.exitCode() == 0) {
                        QFile file(tempFile);
                        if (file.exists() && file.size() > 0) {
                            QImage capturedImage(tempFile);
                            if (!capturedImage.isNull()) {
                                m_screenPixmap = QPixmap::fromImage(capturedImage);
                                qDebug() << "使用外部工具" << cmd << "捕获屏幕成功";
                                captureSuccess = true;
                                QFile::remove(tempFile);
                                break;
                            }
                        }
                    }
                }
            } catch (...) {
                qDebug() << "执行外部命令时捕获到异常";
            }
        }
    }
    
    // 最后，如果所有方法都失败，使用Qt原生方法 - 但在Wayland下可能有问题
    if (!captureSuccess) {
        qDebug() << "所有外部工具捕获失败，尝试使用Qt原生方法 (在Wayland下可能导致缩放问题)";
        
        QList<QScreen*> screens = QGuiApplication::screens();
        
        if (screens.isEmpty()) {
            qDebug() << "错误：无法获取任何屏幕";
            return;
        }
        
        // 计算屏幕边界 - 找到所有屏幕几何区域的联合
        QRect totalGeometry;
        bool firstScreen = true;
        
        for (QScreen *screen : screens) {
            QRect screenGeom = screen->geometry();
            qreal scaleFactor = screen->devicePixelRatio();
            
            qDebug() << "屏幕:" << screen->name() 
                     << "几何区域:" << screenGeom
                     << "分辨率:" << screen->size()
                     << "设备像素比:" << scaleFactor;
            
            // 在Wayland下处理缩放因子
            if (isWayland) {
                qDebug() << "Wayland环境应用缩放因子:" << scaleFactor;
                screenGeom = QRect(
                    screenGeom.x(), 
                    screenGeom.y(),
                    qRound(screenGeom.width() * scaleFactor),
                    qRound(screenGeom.height() * scaleFactor)
                );
            }
            
            if (firstScreen) {
                totalGeometry = screenGeom;
                firstScreen = false;
            } else {
                totalGeometry = totalGeometry.united(screenGeom);
            }
        }
        
        qDebug() << "合并后的屏幕几何区域:" << totalGeometry;
        
        // 创建一个足够大的QPixmap来容纳所有屏幕
        QPixmap combinedPixmap(totalGeometry.size());
        combinedPixmap.fill(Qt::transparent);
        
        QPainter painter(&combinedPixmap);
        
        // 捕获每个屏幕并绘制到正确位置
        for (QScreen *screen : screens) {
            QRect screenGeom = screen->geometry();
            qreal scaleFactor = screen->devicePixelRatio();
            
            // 计算此屏幕相对于合并区域的偏移
            int offsetX = screenGeom.left() - totalGeometry.left();
            int offsetY = screenGeom.top() - totalGeometry.top();
            
            if (isWayland) {
                // 在Wayland下调整偏移量以考虑缩放
                offsetX = qRound(offsetX * scaleFactor);
                offsetY = qRound(offsetY * scaleFactor);
            }
            
            QPoint offset(offsetX, offsetY);
            qDebug() << "尝试捕获屏幕:" << screen->name() 
                     << "偏移:" << offset;
            
            // 捕获此屏幕
            QPixmap screenPixmap;
            
            // 使用grabWindow()还是grabWindow(0)，取决于平台
            try {
                // 使用参数0表示捕获整个屏幕
                screenPixmap = screen->grabWindow(0);
                
                if (!screenPixmap.isNull()) {
                    qDebug() << "屏幕" << screen->name() << "捕获成功，大小:" << screenPixmap.size();
                    
                    // 在Wayland下处理缩放问题
                    if (isWayland && qAbs(scaleFactor - 1.0) > 0.01) {
                        // 在Qt中处理Wayland缩放问题的方法
                        qDebug() << "Wayland环境下，处理截图缩放，原始尺寸:" << screenPixmap.size();
                        QImage img = screenPixmap.toImage();
                        
                        // 将图像缩放到正确的物理尺寸
                        int targetWidth = qRound(img.width() * scaleFactor);
                        int targetHeight = qRound(img.height() * scaleFactor);
                        qDebug() << "调整为物理尺寸:" << QSize(targetWidth, targetHeight);
                        
                        QImage scaledImage = img.scaled(targetWidth, targetHeight, 
                                                       Qt::IgnoreAspectRatio, 
                                                       Qt::SmoothTransformation);
                        screenPixmap = QPixmap::fromImage(scaledImage);
                    }
                    
                    // 绘制到合并的图像中
                    painter.drawPixmap(offset, screenPixmap);
                    captureSuccess = true;
                } else {
                    qDebug() << "屏幕" << screen->name() << "捕获失败";
                }
            } catch (...) {
                qDebug() << "捕获屏幕时发生异常";
            }
        }
        
        painter.end();
        
        if (captureSuccess) {
            m_screenPixmap = combinedPixmap;
            qDebug() << "合并所有屏幕成功，总大小:" << m_screenPixmap.size();
        }
    }
    
    // 最后的尝试 - 如果所有方法都失败
    if (m_screenPixmap.isNull()) {
        QMessageBox::critical(nullptr, "截图失败", 
                             "无法捕获屏幕。\n\n"
                             "您使用的是Wayland显示服务器，请确保安装了以下工具之一:\n"
                             "- grim (推荐)\n"
                             "- spectacle\n"
                             "- gnome-screenshot\n\n"
                             "安装命令: sudo pacman -S grim");
    }
}

void ScreenshotWindow::showAboutDialog()
{
    QMessageBox::about(nullptr, "关于截图工具", 
                      "截图工具 v1.0\n"
                      "用于在Linux系统上进行屏幕截图的工具\n"
                      "支持Wayland和X11环境\n"
                      "\n"
                      "© 2025 截图工具团队");
}

void ScreenshotWindow::saveScreenshot()
{
    if (m_hasSelected && !m_screenPixmap.isNull()) {
        // 获取保存文件路径
        QString filePath = QFileDialog::getSaveFileName(
            this,
            "保存截图",
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + 
                "/screenshot_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss") + ".png",
            "图像文件 (*.png *.jpg *.bmp)");
        
        if (!filePath.isEmpty()) {
            // 获取选择的区域
            QRect rect = selectedRect();
            QPixmap selectedPixmap = m_screenPixmap.copy(rect);
            
            // 将绘制的项目应用到截图上
            if (!m_drawItems.isEmpty()) {
                QPainter painter(&selectedPixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                
                // 相对于选择区域调整绘制位置
                painter.translate(-rect.topLeft());
                
                drawOnPainter(painter);
                painter.end();
            }
            
            // 保存图像
            if (selectedPixmap.save(filePath)) {
                QMessageBox::information(this, "保存成功", "截图已保存到:\n" + filePath);
            } else {
                QMessageBox::critical(this, "保存失败", "无法保存截图到:\n" + filePath);
            }
        }
    }
    
    // 关闭截图窗口
    cancelScreenshot();
}

void ScreenshotWindow::cancelScreenshot()
{
    m_isScreenshotMode = false;
    m_isSelecting = false;
    m_hasSelected = false;
    m_currentMode = DrawMode::None;
    m_drawItems.clear();
    m_undoItems.clear();
    m_rubberBand->hide();
    m_toolBar->hide();
    hide();
    
    // 在Wayland环境下，确保托盘图标重新显示
    if (QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive)) {
        QTimer::singleShot(100, [this]() {
            if (m_trayIcon) {
                m_trayIcon->show();
            }
        });
    } else {
        if (m_trayIcon) {
            m_trayIcon->show();
        }
    }
}

void ScreenshotWindow::finishScreenshot()
{
    if (m_hasSelected && !m_screenPixmap.isNull()) {
        // 获取选择的区域
        QRect rect = selectedRect();
        QPixmap selectedPixmap = m_screenPixmap.copy(rect);
        
        // 将绘制的项目应用到截图上
        if (!m_drawItems.isEmpty()) {
            QPainter painter(&selectedPixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            
            // 相对于选择区域调整绘制位置
            painter.translate(-rect.topLeft());
            
            drawOnPainter(painter);
            painter.end();
        }
        
        // 复制到剪贴板
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setPixmap(selectedPixmap);
        
        // 显示成功消息
        QMessageBox::information(this, "截图完成", "截图已复制到剪贴板");
    }
    
    // 关闭截图窗口
    cancelScreenshot();
}

void ScreenshotWindow::drawRectangle()
{
    // 保护选区状态，防止触发重新选择
    if (m_hasSelected) {
        m_currentMode = DrawMode::Rectangle;
        qDebug() << "切换到矩形绘制模式，保持已选区状态";
    }
}

void ScreenshotWindow::drawCircle()
{
    // 保护选区状态，防止触发重新选择
    if (m_hasSelected) {
        m_currentMode = DrawMode::Circle;
        qDebug() << "切换到圆形绘制模式，保持已选区状态";
    }
}

void ScreenshotWindow::drawArrow()
{
    // 保护选区状态，防止触发重新选择
    if (m_hasSelected) {
        m_currentMode = DrawMode::Arrow;
        qDebug() << "切换到箭头绘制模式，保持已选区状态";
    }
}

void ScreenshotWindow::drawText()
{
    if (!m_hasSelected) return;
    
    bool ok;
    QString text = QInputDialog::getText(this, "输入文字", "请输入要添加的文字:", 
                                        QLineEdit::Normal, "", &ok);
    
    if (ok && !text.isEmpty()) {
        DrawItem item;
        item.mode = DrawMode::Text;
        item.rect = QRect(m_startPoint, m_startPoint);
        item.text = text;
        item.color = Qt::red; // 默认颜色
        m_drawItems.append(item);
        update();
    }
}

void ScreenshotWindow::drawBrush()
{
    // 保护选区状态，防止触发重新选择
    if (m_hasSelected) {
        m_currentMode = DrawMode::Brush;
        qDebug() << "切换到画笔绘制模式，保持已选区状态";
    }
}

void ScreenshotWindow::undo()
{
    if (!m_drawItems.isEmpty()) {
        m_undoItems.append(m_drawItems.takeLast());
        update();
    }
}

void ScreenshotWindow::drawOnPainter(QPainter &painter)
{
    for (const DrawItem &item : m_drawItems) {
        switch (item.mode) {
            case DrawMode::Rectangle: {
                painter.setPen(QPen(Qt::red, 2));
                painter.drawRect(item.rect);
                break;
            }
            case DrawMode::Circle: {
                painter.setPen(QPen(Qt::red, 2));
                painter.drawEllipse(item.rect);
                break;
            }
            case DrawMode::Arrow: {
                // 绘制箭头
                painter.setPen(QPen(Qt::red, 2));
                painter.drawLine(item.start, item.end);
                
                // 计算箭头角度
                QLineF line(item.end, item.start);
                double angle = std::atan2(-line.dy(), line.dx());
                
                // 绘制箭头头部
                QPointF arrowP1 = item.end + QPointF(sin(angle + M_PI / 3) * 10,
                                              cos(angle + M_PI / 3) * 10);
                QPointF arrowP2 = item.end + QPointF(sin(angle + M_PI - M_PI / 3) * 10,
                                              cos(angle + M_PI - M_PI / 3) * 10);
                
                QPolygonF arrowHead;
                arrowHead << item.end << arrowP1 << arrowP2;
                painter.setBrush(Qt::red);
                painter.drawPolygon(arrowHead);
                break;
            }
            case DrawMode::Text: {
                painter.setPen(QPen(Qt::red, 2));
                QFont font = painter.font();
                font.setPointSize(12);
                painter.setFont(font);
                painter.drawText(item.rect.topLeft(), item.text);
                break;
            }
            case DrawMode::Brush: {
                painter.setPen(QPen(Qt::red, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                if (item.brushPoints.size() > 1) {
                    for (int i = 1; i < item.brushPoints.size(); ++i) {
                        painter.drawLine(item.brushPoints[i-1], item.brushPoints[i]);
                    }
                }
                break;
            }
            case DrawMode::Mosaic: {
                // 马赛克效果实现
                int blockSize = 10; // 马赛克块大小
                QRect rect = item.rect.normalized();
                
                for (int y = rect.top(); y < rect.bottom(); y += blockSize) {
                    for (int x = rect.left(); x < rect.right(); x += blockSize) {
                        int w = qMin(blockSize, rect.right() - x);
                        int h = qMin(blockSize, rect.bottom() - y);
                        QRect block(x, y, w, h);
                        
                        // 获取块的平均颜色
                        QRect globalBlock = block.translated(0, 0); // 转换为全局坐标
                        if (globalBlock.left() >= 0 && globalBlock.top() >= 0 &&
                            globalBlock.right() < m_screenPixmap.width() &&
                            globalBlock.bottom() < m_screenPixmap.height()) {
                            
                            QImage blockImage = m_screenPixmap.copy(globalBlock).toImage();
                            int r = 0, g = 0, b = 0, pixels = 0;
                            
                            for (int by = 0; by < blockImage.height(); ++by) {
                                for (int bx = 0; bx < blockImage.width(); ++bx) {
                                    QColor pixelColor = blockImage.pixelColor(bx, by);
                                    r += pixelColor.red();
                                    g += pixelColor.green();
                                    b += pixelColor.blue();
                                    pixels++;
                                }
                            }
                            
                            if (pixels > 0) {
                                QColor avgColor(r / pixels, g / pixels, b / pixels);
                                painter.fillRect(block, avgColor);
                            }
                        }
                    }
                }
                break;
            }
            case DrawMode::None:
                break;
        }
    }
}

QRect ScreenshotWindow::selectedRect() const
{
    QRect rect(m_startPoint, m_endPoint);
    return rect.normalized();
}

void ScreenshotWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    
    if (!m_isScreenshotMode || m_screenPixmap.isNull()) {
        return;
    }
    
    QPainter painter(this);
    
    // 绘制截图
    painter.drawPixmap(0, 0, m_screenPixmap);
    
    // 添加半透明遮罩，突出选择区域
    if (m_hasSelected) {
        QRect selectedRect = this->selectedRect();
        
        // 创建整个屏幕的遮罩层
        QRegion maskRegion(0, 0, width(), height());
        // 从遮罩层中排除已选区域
        QRegion selectedRegion(selectedRect);
        // 最终的遮罩区域是整个屏幕减去选中区域
        m_maskRegion = maskRegion.subtracted(selectedRegion);
        
        // 添加半透明遮罩效果
        painter.setClipRegion(m_maskRegion);
        painter.fillRect(0, 0, width(), height(), QColor(0, 0, 0, 128));
        
        // 清除裁剪区域以便后续绘制
        painter.setClipRegion(QRegion(0, 0, width(), height()));
        
        // 绘制四个角上的小方块，表示选择区域
        int handleSize = 6;
        QPen pen(Qt::white, 2);
        painter.setPen(pen);
        
        // 左上角
        QRect topLeft(selectedRect.left() - handleSize / 2, 
                     selectedRect.top() - handleSize / 2, 
                     handleSize, handleSize);
        // 右上角
        QRect topRight(selectedRect.right() - handleSize / 2, 
                      selectedRect.top() - handleSize / 2, 
                      handleSize, handleSize);
        // 左下角
        QRect bottomLeft(selectedRect.left() - handleSize / 2, 
                        selectedRect.bottom() - handleSize / 2, 
                        handleSize, handleSize);
        // 右下角
        QRect bottomRight(selectedRect.right() - handleSize / 2, 
                         selectedRect.bottom() - handleSize / 2, 
                         handleSize, handleSize);
        
        painter.fillRect(topLeft, Qt::white);
        painter.fillRect(topRight, Qt::white);
        painter.fillRect(bottomLeft, Qt::white);
        painter.fillRect(bottomRight, Qt::white);
        
        // 绘制选择区域的边框
        painter.setPen(QPen(Qt::blue, 1, Qt::SolidLine));
        painter.drawRect(selectedRect);
        
        // 绘制工具栏
        if (m_toolBar) {
            int toolBarX = selectedRect.left();
            int toolBarY = selectedRect.bottom() + 10;
            
            if (toolBarY + m_toolBar->height() > height()) {
                toolBarY = selectedRect.top() - m_toolBar->height() - 10;
                if (toolBarY < 0) {
                    toolBarY = 10;
                }
            }
            
            m_toolBar->move(toolBarX, toolBarY);
            m_toolBar->show();
        }
        
        // 应用已经绘制的项目
        drawOnPainter(painter);
    }
}

void ScreenshotWindow::mousePressEvent(QMouseEvent *event)
{
    // 如果不是截图模式，直接返回
    if (!m_isScreenshotMode) return;

    qDebug() << "鼠标按下，位置:" << event->pos()
             << "m_hasSelected:" << m_hasSelected
             << "m_currentMode:" << static_cast<int>(m_currentMode)
             << "m_isSelecting:" << m_isSelecting;

    if (event->button() == Qt::LeftButton) {
        // 只在未选择区域时允许进入截图选择流程
        if (!m_hasSelected) {
            // 初次截图选择区域时
            m_startPoint = event->pos();
            m_endPoint = event->pos();
            m_isSelecting = true;
            m_rubberBand->setGeometry(QRect(m_startPoint, QSize()));
            m_rubberBand->show();
            qDebug() << "开始选择区域";
        } else {
            // 已经有选区，只允许绘制，不允许重新选择截图区域
            if (m_maskRegion.contains(event->pos())) {
                // 点击在遮罩区域内（即选区外），忽略这次点击事件
                m_isSelecting = false;
                qDebug() << "点击在遮罩区域内(选区外)，忽略此次点击";
                event->accept();
                return;
            } else {
                // 点击在已选区域内，处理绘制操作
                m_startPoint = event->pos();
                m_endPoint = event->pos();
                m_isSelecting = true;
                qDebug() << "在已选区域内开始绘制，当前模式:" << static_cast<int>(m_currentMode);
                // 处理不同的绘图模式
                switch (m_currentMode) {
                    case DrawMode::Rectangle:
                    case DrawMode::Circle:
                    case DrawMode::Arrow:
                        // 这些模式只需要记录起始点
                        break;
                    case DrawMode::Brush:
                        m_currentBrushPoints.clear();
                        m_currentBrushPoints.append(event->pos());
                        break;
                    case DrawMode::None:
                        // 没有选择绘图模式，只是在选区内点击，不做特殊处理
                        break;
                    default:
                        break;
                }
                event->accept();
            }
        }
    }
}

void ScreenshotWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_isScreenshotMode) return;
    
    if (m_isSelecting && (event->buttons() & Qt::LeftButton)) {
        m_endPoint = event->pos();
        
        if (!m_hasSelected) {
            // 更新选择区域（仅在初次选择时）
            m_rubberBand->setGeometry(QRect(m_startPoint, m_endPoint).normalized());
        } else {
            // 已经有选择区域，现在是在绘制
            if (m_currentMode != DrawMode::None) {
                // 所有绘图操作处理方式相同：记录终点并刷新
                switch (m_currentMode) {
                    case DrawMode::Rectangle:
                    case DrawMode::Circle:
                    case DrawMode::Arrow:
                        // 记录终点并更新显示
                        update();
                        break;
                    case DrawMode::Brush:
                        // 添加到当前画笔路径并更新显示
                        m_currentBrushPoints.append(event->pos());
                        update();
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

void ScreenshotWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_isScreenshotMode) return;
    
    qDebug() << "鼠标释放，位置:" << event->pos() 
             << "m_hasSelected:" << m_hasSelected 
             << "m_isSelecting:" << m_isSelecting;
    
    if (event->button() == Qt::LeftButton && m_isSelecting) {
        m_endPoint = event->pos();
        m_isSelecting = false;
        
        if (!m_hasSelected) {
            // 完成初次选择区域
            m_hasSelected = true;
            
            // 确保选择的区域有合理大小
            QRect rect = selectedRect();
            if (rect.width() < 5 || rect.height() < 5) {
                // 如果选择的区域太小，自动扩大
                m_endPoint = m_startPoint + QPoint(100, 100);
            }
            
            m_rubberBand->hide();
            qDebug() << "完成选择区域:" << selectedRect();
        } else if (m_currentMode != DrawMode::None) {
            // 已选择区域 + 处于绘图模式：完成绘制
            QRect selectedArea = selectedRect();
            
            // 只有在起始点在选区内时才添加绘制项目
            // 避免在遮罩区域进行绘制操作
            if (!m_maskRegion.contains(m_startPoint)) {
                // 完成绘制
                DrawItem item;
                item.color = Qt::red; // 默认颜色
                
                qDebug() << "完成绘制，当前模式:" << static_cast<int>(m_currentMode);
                
                switch (m_currentMode) {
                    case DrawMode::Rectangle:
                        item.mode = DrawMode::Rectangle;
                        item.rect = QRect(m_startPoint, m_endPoint).normalized();
                        m_drawItems.append(item);
                        break;
                    case DrawMode::Circle:
                        item.mode = DrawMode::Circle;
                        item.rect = QRect(m_startPoint, m_endPoint).normalized();
                        m_drawItems.append(item);
                        break;
                    case DrawMode::Arrow:
                        item.mode = DrawMode::Arrow;
                        item.start = m_startPoint;
                        item.end = m_endPoint;
                        m_drawItems.append(item);
                        break;
                    case DrawMode::Brush:
                        if (m_currentBrushPoints.size() > 1) {
                            item.mode = DrawMode::Brush;
                            item.brushPoints = m_currentBrushPoints;
                            m_drawItems.append(item);
                        }
                        // 无论是否添加，都要清空轨迹
                        m_currentBrushPoints.clear();
                        break;
                    default:
                        break;
                }
            } else {
                qDebug() << "起始点在遮罩区域内，忽略绘制操作";
                // 也要清空轨迹，防止残留
                if (m_currentMode == DrawMode::Brush) {
                    m_currentBrushPoints.clear();
                }
            }
        }
        
        update();
    }
}

void ScreenshotWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        // 按Esc键取消
        cancelScreenshot();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // 按回车键完成
        if (m_hasSelected) {
            finishScreenshot();
        }
    } else if (event->key() == Qt::Key_C && event->modifiers() == Qt::ControlModifier) {
        // Ctrl+C复制
        if (m_hasSelected) {
            finishScreenshot();
        }
    } else if (event->key() == Qt::Key_S && event->modifiers() == Qt::ControlModifier) {
        // Ctrl+S保存
        if (m_hasSelected) {
            saveScreenshot();
        }
    } else if (event->key() == Qt::Key_Z && event->modifiers() == Qt::ControlModifier) {
        // Ctrl+Z撤销
        undo();
    }
}

void ScreenshotWindow::closeEvent(QCloseEvent *event)
{
    // 在关闭窗口时取消截图
    if (m_isScreenshotMode) {
        cancelScreenshot();
        event->ignore(); // 不实际关闭窗口
    } else {
        event->accept();
    }
}

void ScreenshotWindow::quitApplication()
{
    qDebug() << "退出应用程序";
    
    // 安全退出，先隐藏所有UI元素
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    
    hide();
    
    // 确保不会立即退出（尤其是在Wayland环境中）
    bool isWayland = QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive);
    if (isWayland) {
        qDebug() << "Wayland环境下安全退出";
        // 使用异步延迟退出，让其他操作有机会完成
        QTimer::singleShot(500, []() {
            qDebug() << "执行实际的退出操作";
            QCoreApplication::quit();
        });
    } else {
        QCoreApplication::quit();
    }
}