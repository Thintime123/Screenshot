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
    
    setupTrayIcon();
    
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
    m_trayIconMenu = new QMenu(this);
    
    m_screenshotAction = new QAction("开始截图", this);
    connect(m_screenshotAction, &QAction::triggered, this, &ScreenshotWindow::startScreenshot);
    m_trayIconMenu->addAction(m_screenshotAction);
    
    m_aboutAction = new QAction("关于", this);
    connect(m_aboutAction, &QAction::triggered, this, &ScreenshotWindow::showAboutDialog);
    m_trayIconMenu->addAction(m_aboutAction);
    
    m_trayIconMenu->addSeparator();
    
    m_quitAction = new QAction("退出", this);
    connect(m_quitAction, &QAction::triggered, this, &ScreenshotWindow::quitApplication);
    m_trayIconMenu->addAction(m_quitAction);
    
    m_trayIcon = new QSystemTrayIcon(this);
    
    QIcon icon = QIcon::fromTheme("camera-photo");
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    m_trayIcon->setIcon(icon);
    
    m_trayIcon->setToolTip("截图工具");
    
    m_trayIcon->setContextMenu(m_trayIconMenu);
    
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &ScreenshotWindow::trayIconActivated);
    
    m_trayIcon->show();
}

void ScreenshotWindow::startScreenshot()
{
    m_isScreenshotMode = true;
    m_isSelecting = false;
    m_hasSelected = false;
    m_drawItems.clear();
    m_undoItems.clear();
    
    grabScreen();
    
    showFullScreen();
}

void ScreenshotWindow::grabScreen()
{
    // 获取屏幕截图的更可靠方法
    bool captureSuccess = false;
    
    // 检测当前显示服务器环境
    bool isWayland = QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive);
    qDebug() << "当前平台:" << QGuiApplication::platformName() << (isWayland ? "(Wayland)" : "(可能是X11)");
    
    // 1. 首先尝试使用外部工具方法 (通常在Wayland环境下更可靠)
    QString tempFile = QDir::tempPath() + "/screenshot_" + 
                      QString::number(QRandomGenerator::global()->generate()) + ".png";
    
    // 确保临时文件路径没有非ASCII字符
    if (tempFile.contains(QRegularExpression("[^\\x00-\\x7F]"))) {
        tempFile = "/tmp/screenshot_" + 
                  QString::number(QRandomGenerator::global()->generate()) + ".png";
    }
    
    qDebug() << "使用临时文件:" << tempFile;
    
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
    
    if (isWayland && QFile::exists("/usr/bin/grim")) {
        possibleCmds << "/usr/bin/grim \"" + tempFile + "\"";
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
                        qDebug() << "临时文件创建成功，大小:" << file.size() << "字节";
                        
                        // 加载临时文件
                        QImage capturedImage(tempFile);
                        if (!capturedImage.isNull()) {
                            m_screenPixmap = QPixmap::fromImage(capturedImage);
                            qDebug() << "使用外部工具" << cmd << "捕获屏幕成功";
                            captureSuccess = true;
                            
                            // 为安全起见，删除临时文件
                            QFile::remove(tempFile);
                            break;
                        } else {
                            qDebug() << "临时文件创建成功但加载失败";
                        }
                    } else {
                        qDebug() << "临时文件不存在或为空";
                    }
                } else {
                    qDebug() << "命令执行失败:" << process.readAll();
                }
            } else {
                qDebug() << "命令执行超时";
            }
        } catch (const std::exception& e) {
            qDebug() << "执行外部命令时捕获到异常:" << e.what();
        } catch (...) {
            qDebug() << "执行外部命令时捕获到未知异常";
        }
    }
    
    // 2. 如果外部工具方法失败，尝试Qt原生方法
    if (!captureSuccess) {
        qDebug() << "所有外部工具捕获失败，尝试Qt原生方法";
        
        QList<QScreen*> screens = QGuiApplication::screens();
        
        if (screens.isEmpty()) {
            qDebug() << "错误：无法获取任何屏幕";
            return;
        }
        
        // 获取主屏幕几何信息作为基准
        QScreen *primaryScreen = QGuiApplication::primaryScreen();
        QRect totalGeometry = primaryScreen->geometry();
        
        // 计算所有屏幕的总几何区域
        for (QScreen *screen : screens) {
            totalGeometry = totalGeometry.united(screen->geometry());
        }
        
        qDebug() << "合并的屏幕几何区域:" << totalGeometry;
        
        // 创建一个足够大的QPixmap来容纳所有屏幕
        m_screenPixmap = QPixmap(totalGeometry.width(), totalGeometry.height());
        m_screenPixmap.fill(Qt::black); // 填充背景，以防某些区域没有覆盖到
        
        // 在合并的屏幕区域上绘制各个屏幕的内容
        QPainter painter(&m_screenPixmap);
        
        bool anyScreenCaptured = false;
        
        for (QScreen *screen : screens) {
            QRect screenGeometry = screen->geometry();
            qDebug() << "尝试捕获屏幕:" << screen->name() << "几何区域:" << screenGeometry;
            
            // 尝试捕获此屏幕
            QPixmap screenPixmap;
            
            // 尝试直接从screen捕获
            try {
                screenPixmap = screen->grabWindow(0);
                
                if (screenPixmap.isNull()) {
                    qDebug() << "通过screen->grabWindow(0)捕获屏幕" << screen->name() << "失败，尝试替代方法";
                    
                    // 尝试替代方法 - 限定区域捕获
                    screenPixmap = screen->grabWindow(0, 0, 0, screenGeometry.width(), screenGeometry.height());
                }
            } catch (const std::exception& e) {
                qDebug() << "捕获屏幕" << screen->name() << "时发生异常:" << e.what();
                screenPixmap = QPixmap(); // 确保是空的
            } catch (...) {
                qDebug() << "捕获屏幕" << screen->name() << "时发生未知异常";
                screenPixmap = QPixmap(); // 确保是空的
            }
            
            // 如果此屏幕捕获成功，绘制到合并的图像中
            if (!screenPixmap.isNull()) {
                // 计算此屏幕相对于合并区域的偏移
                QPoint offset = screenGeometry.topLeft() - totalGeometry.topLeft();
                
                qDebug() << "屏幕" << screen->name() << "捕获成功，大小:" << screenPixmap.size();
                qDebug() << "绘制到位置:" << offset;
                
                // 将此屏幕图像绘制到合适的位置
                painter.drawPixmap(offset, screenPixmap);
                anyScreenCaptured = true;
            } else {
                qDebug() << "警告：屏幕" << screen->name() << "捕获失败";
            }
        }
        
        painter.end();
        
        // 如果所有屏幕都捕获失败，尝试备用方法
        if (!anyScreenCaptured) {
            qDebug() << "所有屏幕捕获都失败，尝试备用方法";
            
            // 回到最简单的方法，只捕获主屏幕
            try {
                m_screenPixmap = primaryScreen->grabWindow(0);
                
                if (m_screenPixmap.isNull()) {
                    qDebug() << "主屏幕备用捕获方法1失败，尝试方法2";
                    m_screenPixmap = primaryScreen->grabWindow(0, 0, 0, primaryScreen->geometry().width(), primaryScreen->geometry().height());
                }
            } catch (...) {
                qDebug() << "主屏幕备用捕获也失败";
                m_screenPixmap = QPixmap(); // 确保是空的
            }
        }
        
        if (!m_screenPixmap.isNull()) {
            captureSuccess = true;
        }
    }
    
    // 3. 如果以上方法都失败，尝试使用XDG-Desktop-Portal（适用于Wayland）
    if (!captureSuccess && isWayland) {
        qDebug() << "尝试使用XDG-Desktop-Portal方法(DBus方式)";
        
        // 为了简化实现，使用临时脚本来调用xdg-desktop-portal
        QString scriptFile = QDir::tempPath() + "/screenshot_helper_" + 
                           QString::number(QRandomGenerator::global()->generate()) + ".sh";
        
        QFile file(scriptFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "#!/bin/bash\n";
            out << "dbus-send --session --print-reply --dest=org.freedesktop.portal.Desktop "
                   "/org/freedesktop/portal/desktop org.freedesktop.portal.Screenshot.Screenshot "
                   "boolean:true string:\"" << tempFile << "\" > /dev/null 2>&1\n";
            out << "sleep 3\n";  // 给用户时间选择区域
            out << "exit 0\n";
            file.close();
            
            QFile::setPermissions(scriptFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
            
            QProcess process;
            process.start("bash", QStringList() << scriptFile);
            
            if (process.waitForFinished(10000)) {
                if (QFile::exists(tempFile)) {
                    QImage capturedImage(tempFile);
                    if (!capturedImage.isNull()) {
                        m_screenPixmap = QPixmap::fromImage(capturedImage);
                        qDebug() << "使用XDG-Desktop-Portal方法捕获屏幕成功";
                        captureSuccess = true;
                    }
                }
            }
            
            QFile::remove(scriptFile);
            QFile::remove(tempFile);
        }
    }
    
    // 最后检查是否成功
    if (m_screenPixmap.isNull()) {
        qDebug() << "错误：无法捕获任何屏幕";
        QMessageBox::critical(nullptr, "截图失败", "无法捕获屏幕内容，请检查您的显示设置或尝试重新启动应用程序。\n\n"
                              "如果您使用的是Wayland显示服务器，请确保安装了以下工具之一：\n"
                              "- gnome-screenshot\n"
                              "- ksnip\n"
                              "- spectacle\n"
                              "- scrot\n"
                              "- grim (推荐用于Wayland)\n");
    } else {
        qDebug() << "屏幕捕获成功，总大小:" << m_screenPixmap.size();
    }
}

void ScreenshotWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        startScreenshot();
    }
}

void ScreenshotWindow::showAboutDialog()
{
    QMessageBox::about(nullptr, "关于截图工具", 
                       "截图工具 v1.0\n\n"
                       "这是一个简单的截图工具，可以捕获屏幕并进行编辑。\n\n"
                       "使用方法：\n"
                       "- 点击托盘图标开始截图\n"
                       "- 拖动鼠标选择区域\n"
                       "- 使用工具栏添加标注\n"
                       "- 保存或复制到剪贴板\n\n"
                       "快捷键：\n"
                       "- Esc: 取消截图\n"
                       "- Enter: 完成截图\n"
                       "- Ctrl+Z: 撤销\n"
                       "- Ctrl+S: 保存");
}

void ScreenshotWindow::quitApplication()
{
    QApplication::quit();
}

void ScreenshotWindow::closeEvent(QCloseEvent *event)
{
    if (m_isScreenshotMode) {
        m_isScreenshotMode = false;
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}

void ScreenshotWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    
    painter.drawPixmap(0, 0, m_screenPixmap);
    
    if (m_hasSelected) {
        QRect selectedArea = selectedRect();
        
        QColor maskColor(0, 0, 0, 120);
        painter.setBrush(maskColor);
        painter.setPen(Qt::NoPen);
        
        QPainterPath path;
        path.addRect(rect());
        path.addRect(selectedArea);
        painter.drawPath(path);
        
        painter.setPen(QPen(Qt::red, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selectedArea);
        
        drawOnPainter(painter);
        
        QString sizeText = QString("%1 x %2").arg(selectedArea.width()).arg(selectedArea.height());
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(selectedArea.topLeft() + QPoint(5, -5), sizeText);
    } else if (m_isSelecting) {
        QRect selectedArea = selectedRect();
        
        QColor maskColor(0, 0, 0, 120);
        painter.setBrush(maskColor);
        painter.setPen(Qt::NoPen);
        
        QPainterPath path;
        path.addRect(rect());
        path.addRect(selectedArea);
        painter.drawPath(path);
        
        painter.setPen(QPen(Qt::red, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selectedArea);
        
        QString sizeText = QString("%1 x %2").arg(selectedArea.width()).arg(selectedArea.height());
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(selectedArea.topLeft() + QPoint(5, -5), sizeText);
    } else {
        QColor maskColor(0, 0, 0, 120);
        painter.fillRect(rect(), maskColor);
        
        QString helpText = "按下鼠标左键并拖动来选择截图区域\n按ESC取消截图";
        QFont font = painter.font();
        font.setBold(true);
        font.setPointSize(12);
        painter.setFont(font);
        painter.setPen(Qt::white);
        
        QFontMetrics metrics(font);
        QRect textRect = metrics.boundingRect(rect(), Qt::AlignCenter, helpText);
        painter.drawText(textRect, helpText);
    }
}

void ScreenshotWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!m_hasSelected) {
            m_isSelecting = true;
            m_startPoint = event->pos();
            m_endPoint = m_startPoint;
            m_rubberBand->setGeometry(QRect(m_startPoint, QSize()));
            m_rubberBand->show();
        } else {
            if (m_currentMode != DrawMode::None) {
                m_startPoint = event->pos();
                m_endPoint = m_startPoint;
                
                if (!selectedRect().contains(m_startPoint)) {
                    return;
                }
                
                if (m_currentMode == DrawMode::Text) {
                    bool ok;
                    QString text = QInputDialog::getText(this, "输入文字", 
                                                         "请输入要添加的文字：", 
                                                         QLineEdit::Normal, 
                                                         "", &ok);
                    if (ok && !text.isEmpty()) {
                        DrawItem item;
                        item.mode = DrawMode::Text;
                        item.start = m_startPoint;
                        item.text = text;
                        item.color = Qt::red;
                        m_drawItems.append(item);
                        update();
                    }
                } else if (m_currentMode == DrawMode::Brush) {
                    m_currentBrushPoints.clear();
                    m_currentBrushPoints.append(event->pos());
                    update();
                }
            }
        }
    }
}

void ScreenshotWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isSelecting) {
        m_endPoint = event->pos();
        m_rubberBand->setGeometry(selectedRect());
        update();
    } else if (m_hasSelected && m_currentMode != DrawMode::None && m_currentMode != DrawMode::Text) {
        if (event->buttons() & Qt::LeftButton) {
            m_endPoint = event->pos();
            
            if (m_currentMode == DrawMode::Brush && selectedRect().contains(event->pos())) {
                m_currentBrushPoints.append(event->pos());
            }
            
            update();
        }
    }
}

void ScreenshotWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_isSelecting) {
            m_isSelecting = false;
            m_hasSelected = true;
            m_endPoint = event->pos();
            
            QRect selectedArea = selectedRect();
            m_toolBar->move(selectedArea.right() - m_toolBar->width(), 
                            selectedArea.bottom() + 5);
            m_toolBar->setVisible(true);
            
            update();
        } else if (m_hasSelected && m_currentMode != DrawMode::None && m_currentMode != DrawMode::Text) {
            m_endPoint = event->pos();
            
            if (selectedRect().contains(m_startPoint)) {
                DrawItem item;
                item.mode = m_currentMode;
                item.start = m_startPoint;
                item.end = m_endPoint;
                item.rect = QRect(m_startPoint, m_endPoint).normalized();
                item.color = Qt::red;
                
                if (m_currentMode == DrawMode::Brush && !m_currentBrushPoints.isEmpty()) {
                    item.brushPoints = m_currentBrushPoints;
                    m_currentBrushPoints.clear();
                }
                
                m_drawItems.append(item);
            }
            
            update();
        }
    }
}

void ScreenshotWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelScreenshot();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_hasSelected) {
            finishScreenshot();
        }
    } else if (event->key() == Qt::Key_Z && event->modifiers() == Qt::ControlModifier) {
        undo();
    } else if (event->key() == Qt::Key_S && event->modifiers() == Qt::ControlModifier) {
        if (m_hasSelected) {
            saveScreenshot();
        }
    }
}

void ScreenshotWindow::saveScreenshot()
{
    if (!m_hasSelected) {
        return;
    }
    
    QRect selectedArea = selectedRect();
    
    QPixmap screenshot = m_screenPixmap.copy(selectedArea);
    
    QPainter painter(&screenshot);
    
    painter.translate(-selectedArea.topLeft());
    
    for (const DrawItem &item : m_drawItems) {
        if (selectedArea.contains(item.start)) {
            painter.setPen(QPen(item.color, 2));
            
            switch (item.mode) {
            case DrawMode::Rectangle:
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(QRect(item.start, item.end).normalized());
                break;
            case DrawMode::Circle:
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(QRect(item.start, item.end).normalized());
                break;
            case DrawMode::Arrow: {
                QLineF line(item.start, item.end);
                painter.drawLine(line);
                
                double angle = std::atan2(-line.dy(), line.dx());
                QPointF arrowP1 = item.end + QPointF(qCos(angle + M_PI * 3/4) * 10,
                                                      qSin(angle + M_PI * 3/4) * 10);
                QPointF arrowP2 = item.end + QPointF(qCos(angle + M_PI * 5/4) * 10,
                                                      qSin(angle + M_PI * 5/4) * 10);
                QPolygonF arrowHead;
                arrowHead << item.end << arrowP1 << arrowP2;
                painter.setBrush(item.color);
                painter.drawPolygon(arrowHead);
                break;
            }
            case DrawMode::Text:
                painter.setFont(QFont("Arial", 12, QFont::Bold));
                painter.drawText(item.start, item.text);
                break;
            case DrawMode::Brush:
                if (!item.brushPoints.isEmpty()) {
                    painter.setPen(QPen(item.color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    
                    if (item.brushPoints.size() == 1) {
                        painter.drawPoint(item.brushPoints.first());
                    } else {
                        for (int i = 1; i < item.brushPoints.size(); ++i) {
                            painter.drawLine(item.brushPoints[i-1], item.brushPoints[i]);
                        }
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    
    QString defaultFilePath = QDir::homePath() + "/Pictures/screenshot_" + 
                              QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + 
                              ".png";
    
    QString filePath = QFileDialog::getSaveFileName(this, "保存截图", 
                                                    defaultFilePath, 
                                                    "PNG图像 (*.png);;JPEG图像 (*.jpg)");
    
    if (!filePath.isEmpty()) {
        if (screenshot.save(filePath)) {
            QMessageBox::information(this, "保存成功", "截图已保存到: " + filePath);
            close();
        } else {
            QMessageBox::warning(this, "保存失败", "保存截图时出错");
        }
    }
}

void ScreenshotWindow::cancelScreenshot()
{
    m_isScreenshotMode = false;
    hide();
}

void ScreenshotWindow::finishScreenshot()
{
    if (m_hasSelected) {
        QRect selectedArea = selectedRect();
        qDebug() << "截图区域:" << selectedArea;
        
        if (selectedArea.width() <= 0 || selectedArea.height() <= 0) {
            QMessageBox::warning(this, "无效截图", "选择的区域无效，请重新选择");
            m_isScreenshotMode = false;
            hide();
            return;
        }
        
        // 检查截图区域是否在屏幕内
        QScreen *screen = QGuiApplication::primaryScreen();
        QRect screenGeometry = screen->geometry();
        qDebug() << "屏幕几何尺寸:" << screenGeometry;
        
        if (!screenGeometry.contains(selectedArea)) {
            qDebug() << "警告：截图区域部分在屏幕外，调整区域到屏幕内";
            selectedArea = selectedArea.intersected(screenGeometry);
            qDebug() << "调整后的截图区域:" << selectedArea;
        }
        
        // 检查原始截图是否有效
        if (m_screenPixmap.isNull()) {
            qDebug() << "错误：原始截图为空";
            QMessageBox::critical(this, "截图失败", "截图初始化失败，原始屏幕图像为空");
            m_isScreenshotMode = false;
            hide();
            return;
        }
        
        qDebug() << "原始截图尺寸:" << m_screenPixmap.size() << "深度:" << m_screenPixmap.depth();
        
        // 创建截图 - 使用安全的创建方式
        QPixmap screenshot;
        
        // 安全的截图创建尝试
        try {
            if (selectedArea.right() > m_screenPixmap.width() || 
                selectedArea.bottom() > m_screenPixmap.height()) {
                qDebug() << "修正：选择区域超出原始图像大小";
                selectedArea = selectedArea.intersected(QRect(0, 0, m_screenPixmap.width(), m_screenPixmap.height()));
                qDebug() << "修正后的区域:" << selectedArea;
            }
            
            // 创建截图前再次检查参数合法性
            if (selectedArea.width() <= 0 || selectedArea.height() <= 0 || 
                selectedArea.right() > m_screenPixmap.width() || 
                selectedArea.bottom() > m_screenPixmap.height()) {
                qDebug() << "错误：截图区域参数无效";
                QMessageBox::critical(this, "截图失败", "无法创建截图：区域参数无效");
                m_isScreenshotMode = false;
                hide();
                return;
            }
            
            qDebug() << "尝试创建截图，区域:" << selectedArea;
            screenshot = m_screenPixmap.copy(selectedArea);
            
            if (screenshot.isNull()) {
                qDebug() << "错误：创建的截图为空";
                QMessageBox::critical(this, "截图失败", "无法创建截图：操作失败");
                m_isScreenshotMode = false;
                hide();
                return;
            }
            
            qDebug() << "截图创建成功，尺寸:" << screenshot.size();
        } catch (const std::exception& e) {
            qDebug() << "创建截图时捕获到异常:" << e.what();
            QMessageBox::critical(this, "截图失败", QString("创建截图时发生错误: %1").arg(e.what()));
            m_isScreenshotMode = false;
            hide();
            return;
        } catch (...) {
            qDebug() << "创建截图时捕获到未知异常";
            QMessageBox::critical(this, "截图失败", "创建截图时发生未知错误");
            m_isScreenshotMode = false;
            hide();
            return;
        }
        
        // 尝试直接使用截图
        QImage testImage = screenshot.toImage();
        if (testImage.isNull()) {
            qDebug() << "错误：截图转换为QImage失败";
            QMessageBox::critical(this, "截图失败", "截图处理失败");
            m_isScreenshotMode = false;
            hide();
            return;
        }
        
        // 在截图上绘制用户添加的图形
        QPainter painter;
        bool painterStarted = painter.begin(&screenshot);
        
        if (!painterStarted) {
            qDebug() << "警告：无法在截图上开始绘制，跳过绘制操作";
            // 继续使用原始截图，不尝试绘制
        } else {
            painter.translate(-selectedArea.topLeft());
            
            // 绘制用户添加的图形
            for (const DrawItem &item : m_drawItems) {
                painter.setPen(QPen(item.color, 2));
                
                switch (item.mode) {
                case DrawMode::Rectangle:
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(QRect(item.start, item.end).normalized());
                    break;
                case DrawMode::Circle:
                    painter.setBrush(Qt::NoBrush);
                    painter.drawEllipse(QRect(item.start, item.end).normalized());
                    break;
                case DrawMode::Arrow: {
                    QLineF line(item.start, item.end);
                    painter.drawLine(line);
                    
                    double angle = std::atan2(-line.dy(), line.dx());
                    QPointF arrowP1 = item.end + QPointF(qCos(angle + M_PI * 3/4) * 10,
                                                          qSin(angle + M_PI * 3/4) * 10);
                    QPointF arrowP2 = item.end + QPointF(qCos(angle + M_PI * 5/4) * 10,
                                                          qSin(angle + M_PI * 5/4) * 10);
                    QPolygonF arrowHead;
                    arrowHead << item.end << arrowP1 << arrowP2;
                    painter.setBrush(item.color);
                    painter.drawPolygon(arrowHead);
                    break;
                }
                case DrawMode::Text:
                    painter.setFont(QFont("Arial", 12, QFont::Bold));
                    painter.drawText(item.start, item.text);
                    break;
                case DrawMode::Brush:
                    if (!item.brushPoints.isEmpty()) {
                        painter.setPen(QPen(item.color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                        
                        if (item.brushPoints.size() == 1) {
                            painter.drawPoint(item.brushPoints.first());
                        } else {
                            for (int i = 1; i < item.brushPoints.size(); ++i) {
                                painter.drawLine(item.brushPoints[i-1], item.brushPoints[i]);
                            }
                        }
                    }
                    break;
                default:
                    break;
                }
            }
            
            painter.end();
        }
        
        // 保存截图
        // 创建一个基本路径
        QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        
        // 检查路径存在性
        if (picturesPath.isEmpty() || !QDir(picturesPath).exists()) {
            qDebug() << "Pictures路径不存在，尝试桌面路径";
            picturesPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        }
        
        // 检查中文桌面路径
        if (picturesPath.isEmpty() || !QDir(picturesPath).exists()) {
            QString homePath = QDir::homePath();
            QStringList possiblePaths = {
                homePath + "/桌面",
                homePath + "/Desktop"
            };
            
            for (const QString &path : possiblePaths) {
                if (QDir(path).exists()) {
                    qDebug() << "找到有效桌面路径:" << path;
                    picturesPath = path;
                    break;
                }
            }
        }
        
        // 最后的保障 - 使用用户主目录
        if (picturesPath.isEmpty() || !QDir(picturesPath).exists()) {
            qDebug() << "使用用户主目录作为最后的保障";
            picturesPath = QDir::homePath();
        }
        
        qDebug() << "将使用保存路径基础目录:" << picturesPath;
        
        // 检查目录权限
        QFileInfo dirInfo(picturesPath);
        if (!dirInfo.isWritable()) {
            qDebug() << "警告：选择的目录不可写，将尝试程序目录";
            picturesPath = qApp->applicationDirPath();
            qDebug() << "改用程序目录:" << picturesPath;
        }
        
        // 创建唯一文件名
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
        QString filename = QString("截图_%1.png").arg(timestamp);
        QString savePath = picturesPath + "/" + filename;
        
        qDebug() << "最终保存路径:" << savePath;
        
        // 安全保存截图
        bool saveSuccess = false;
        
        try {
            // 先进行内存图像测试
            QByteArray byteArray;
            QBuffer buffer(&byteArray);
            buffer.open(QIODevice::WriteOnly);
            
            if (screenshot.save(&buffer, "PNG")) {
                qDebug() << "内存中PNG创建成功，大小:" << byteArray.size() << "字节";
                buffer.close();
                
                // 尝试实际文件保存
                QFile file(savePath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(byteArray);
                    file.close();
                    saveSuccess = true;
                    qDebug() << "文件保存成功";
                } else {
                    qDebug() << "文件打开失败:" << file.errorString();
                }
            } else {
                qDebug() << "内存中PNG创建失败";
            }
        } catch (const std::exception& e) {
            qDebug() << "保存截图时捕获到异常:" << e.what();
        } catch (...) {
            qDebug() << "保存截图时捕获到未知异常";
        }
        
        // 如果第一次保存失败，尝试直接保存到用户主目录
        if (!saveSuccess) {
            qDebug() << "第一次保存失败，尝试直接保存到用户主目录";
            savePath = QDir::homePath() + "/" + filename;
            try {
                saveSuccess = screenshot.save(savePath, "PNG");
                qDebug() << "用户主目录保存" << (saveSuccess ? "成功" : "失败");
            } catch (...) {
                qDebug() << "保存到用户主目录时发生异常";
            }
        }
        
        // 最后尝试程序目录
        if (!saveSuccess) {
            qDebug() << "尝试保存到程序目录";
            savePath = qApp->applicationDirPath() + "/" + filename;
            try {
                saveSuccess = screenshot.save(savePath, "PNG");
                qDebug() << "程序目录保存" << (saveSuccess ? "成功" : "失败");
            } catch (...) {
                qDebug() << "保存到程序目录时发生异常";
            }
        }
        
        // 如果所有尝试都失败，尝试一个非常基本的方法
        if (!saveSuccess) {
            qDebug() << "尝试最基本的保存方法 - 直接保存到/tmp";
            savePath = "/tmp/" + filename;
            try {
                // 使用QImage作为中间步骤
                QImage image = screenshot.toImage();
                if (!image.isNull()) {
                    saveSuccess = image.save(savePath, "PNG");
                    qDebug() << "通过QImage保存到/tmp " << (saveSuccess ? "成功" : "失败");
                }
            } catch (...) {
                qDebug() << "基本保存方法也失败";
            }
        }
        
        if (saveSuccess) {
            // 剪贴板处理
            bool clipboardSuccess = false;
            
            try {
                QClipboard *clipboard = QApplication::clipboard();
                if (clipboard) {
                    QImage image = screenshot.toImage();
                    clipboard->setImage(image);
                    QApplication::processEvents();
                    clipboardSuccess = true;
                    qDebug() << "QClipboard设置图像成功";
                }
            } catch (...) {
                qDebug() << "设置剪贴板时发生异常";
            }
            
            // 在Wayland环境使用wl-copy
            if (QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive) && 
                QFile::exists("/usr/bin/wl-copy")) {
                
                try {
                    QProcess process;
                    process.setProcessChannelMode(QProcess::MergedChannels);
                    
                    QString cmd = QString("/usr/bin/wl-copy -t image/png < \"%1\"").arg(savePath);
                    qDebug() << "执行wl-copy命令:" << cmd;
                    
                    process.start("bash", QStringList() << "-c" << cmd);
                    if (process.waitForFinished(3000)) {
                        if (process.exitCode() == 0) {
                            clipboardSuccess = true;
                            qDebug() << "wl-copy命令成功";
                        } else {
                            qDebug() << "wl-copy命令失败:" << process.readAll();
                        }
                    }
                } catch (...) {
                    qDebug() << "执行wl-copy命令时发生异常";
                }
            }
            
            // 显示成功消息
            QString message = "截图已保存到:\n" + savePath;
            if (clipboardSuccess) {
                message += "\n\n且已复制到剪贴板，可使用Ctrl+V粘贴";
            } else {
                message += "\n\n但复制到剪贴板可能失败，您可以手动复制此文件";
            }
            
            QMessageBox::information(this, "截图完成", message);
            
            // 尝试打开文件所在目录
            try {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(savePath).absolutePath()));
            } catch (...) {
                qDebug() << "打开文件目录时发生异常";
            }
        } else {
            QMessageBox::critical(this, "截图失败", 
                              "无法保存截图文件。\n请确保您有足够的权限和磁盘空间。");
        }
    }
    
    // 隐藏窗口
    m_isScreenshotMode = false;
    hide();
}

void ScreenshotWindow::drawRectangle()
{
    m_currentMode = DrawMode::Rectangle;
}

void ScreenshotWindow::drawCircle()
{
    m_currentMode = DrawMode::Circle;
}

void ScreenshotWindow::drawArrow()
{
    m_currentMode = DrawMode::Arrow;
}

void ScreenshotWindow::drawText()
{
    m_currentMode = DrawMode::Text;
}

void ScreenshotWindow::drawBrush()
{
    m_currentMode = DrawMode::Brush;
    m_currentBrushPoints.clear();
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
        painter.setPen(QPen(item.color, 2));
        
        switch (item.mode) {
        case DrawMode::Rectangle:
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRect(item.start, item.end).normalized());
            break;
        case DrawMode::Circle:
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QRect(item.start, item.end).normalized());
            break;
        case DrawMode::Arrow: {
            QLineF line(item.start, item.end);
            painter.drawLine(line);
            
            double angle = std::atan2(-line.dy(), line.dx());
            QPointF arrowP1 = item.end + QPointF(qCos(angle + M_PI * 3/4) * 10,
                                                  qSin(angle + M_PI * 3/4) * 10);
            QPointF arrowP2 = item.end + QPointF(qCos(angle + M_PI * 5/4) * 10,
                                                  qSin(angle + M_PI * 5/4) * 10);
            QPolygonF arrowHead;
            arrowHead << item.end << arrowP1 << arrowP2;
            painter.setBrush(item.color);
            painter.drawPolygon(arrowHead);
            break;
        }
        case DrawMode::Text:
            painter.setFont(QFont("Arial", 12, QFont::Bold));
            painter.drawText(item.start, item.text);
            break;
        case DrawMode::Brush:
            if (!item.brushPoints.isEmpty()) {
                painter.setPen(QPen(item.color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                
                if (item.brushPoints.size() == 1) {
                    painter.drawPoint(item.brushPoints.first());
                } else {
                    for (int i = 1; i < item.brushPoints.size(); ++i) {
                        painter.drawLine(item.brushPoints[i-1], item.brushPoints[i]);
                    }
                }
            }
            break;
        default:
            break;
        }
    }
    
    if (m_currentMode == DrawMode::Brush && !m_currentBrushPoints.isEmpty()) {
        painter.setPen(QPen(Qt::red, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        
        if (m_currentBrushPoints.size() == 1) {
            painter.drawPoint(m_currentBrushPoints.first());
        } else {
            for (int i = 1; i < m_currentBrushPoints.size(); ++i) {
                painter.drawLine(m_currentBrushPoints[i-1], m_currentBrushPoints[i]);
            }
        }
    }
}

QRect ScreenshotWindow::selectedRect() const
{
    return QRect(m_startPoint, m_endPoint).normalized();
}