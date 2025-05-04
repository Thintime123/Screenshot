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
#include <QTimer> // 添加定时器支持
#include <QEventLoop> // 添加事件循环支持
#include <QBuffer> // 添加图像缓冲区支持
#include <QMimeData> // 添加MIME数据支持

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
    // 设置窗口属性
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    
    // 创建并设置工具栏
    m_toolBar->setVisible(false);
    m_toolBar->setFixedHeight(40);
    
    // 创建工具栏动作
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
    
    // 连接工具栏信号槽
    connect(m_rectAction, &QAction::triggered, this, &ScreenshotWindow::drawRectangle);
    connect(m_circleAction, &QAction::triggered, this, &ScreenshotWindow::drawCircle);
    connect(m_arrowAction, &QAction::triggered, this, &ScreenshotWindow::drawArrow);
    connect(m_textAction, &QAction::triggered, this, &ScreenshotWindow::drawText);
    connect(m_brushAction, &QAction::triggered, this, &ScreenshotWindow::drawBrush); // 连接画笔动作
    connect(m_undoAction, &QAction::triggered, this, &ScreenshotWindow::undo);
    connect(m_saveAction, &QAction::triggered, this, &ScreenshotWindow::saveScreenshot);
    connect(m_cancelAction, &QAction::triggered, this, &ScreenshotWindow::cancelScreenshot);
    connect(m_finishAction, &QAction::triggered, this, &ScreenshotWindow::finishScreenshot);
    
    // 设置系统托盘图标
    setupTrayIcon();
    
    // 默认隐藏窗口，仅显示托盘图标
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
    // 创建托盘菜单
    m_trayIconMenu = new QMenu(this);
    
    // 添加截图动作
    m_screenshotAction = new QAction("开始截图", this);
    connect(m_screenshotAction, &QAction::triggered, this, &ScreenshotWindow::startScreenshot);
    m_trayIconMenu->addAction(m_screenshotAction);
    
    // 添加关于动作
    m_aboutAction = new QAction("关于", this);
    connect(m_aboutAction, &QAction::triggered, this, &ScreenshotWindow::showAboutDialog);
    m_trayIconMenu->addAction(m_aboutAction);
    
    // 添加分隔线
    m_trayIconMenu->addSeparator();
    
    // 添加退出动作
    m_quitAction = new QAction("退出", this);
    connect(m_quitAction, &QAction::triggered, this, &ScreenshotWindow::quitApplication);
    m_trayIconMenu->addAction(m_quitAction);
    
    // 创建系统托盘图标
    m_trayIcon = new QSystemTrayIcon(this);
    
    // 设置图标 - 先使用应用程序图标，之后可以替换为自定义图标
    QIcon icon = QIcon::fromTheme("camera-photo");
    if (icon.isNull()) {
        // 如果系统主题图标不可用，尝试使用内置图标
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    m_trayIcon->setIcon(icon);
    
    // 设置托盘图标工具提示
    m_trayIcon->setToolTip("截图工具");
    
    // 设置托盘菜单
    m_trayIcon->setContextMenu(m_trayIconMenu);
    
    // 连接托盘图标点击信号
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &ScreenshotWindow::trayIconActivated);
    
    // 显示托盘图标
    m_trayIcon->show();
}

void ScreenshotWindow::startScreenshot()
{
    // 设置为截图模式
    m_isScreenshotMode = true;
    m_isSelecting = false;
    m_hasSelected = false;
    m_drawItems.clear();
    m_undoItems.clear();
    
    // 截取屏幕
    grabScreen();
    
    // 显示窗口
    showFullScreen();
}

void ScreenshotWindow::grabScreen()
{
    // 获取当前屏幕
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return;
    }
    
    // 捕获整个屏幕
    m_screenPixmap = screen->grabWindow(0);
}

void ScreenshotWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        // 单击托盘图标开始截图
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
        // 如果是在截图模式下，隐藏窗口而不是关闭应用程序
        m_isScreenshotMode = false;
        hide();
        event->ignore();
    } else {
        // 正常关闭应用程序
        event->accept();
    }
}

void ScreenshotWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    
    // 绘制屏幕截图
    painter.drawPixmap(0, 0, m_screenPixmap);
    
    // 如果已经选择了区域
    if (m_hasSelected) {
        QRect selectedArea = selectedRect();
        
        // 绘制半透明遮罩
        QColor maskColor(0, 0, 0, 120);
        painter.setBrush(maskColor);
        painter.setPen(Qt::NoPen);
        
        // 绘制遮罩区域（整个屏幕减去选择区域）
        QPainterPath path;
        path.addRect(rect());
        path.addRect(selectedArea);
        painter.drawPath(path);
        
        // 绘制选择区域边框
        painter.setPen(QPen(Qt::red, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selectedArea);
        
        // 绘制用户添加的图形
        drawOnPainter(painter);
        
        // 绘制尺寸提示
        QString sizeText = QString("%1 x %2").arg(selectedArea.width()).arg(selectedArea.height());
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(selectedArea.topLeft() + QPoint(5, -5), sizeText);
    } else if (m_isSelecting) {
        // 绘制选择中的矩形区域
        QRect selectedArea = selectedRect();
        
        // 绘制半透明遮罩
        QColor maskColor(0, 0, 0, 120);
        painter.setBrush(maskColor);
        painter.setPen(Qt::NoPen);
        
        // 绘制遮罩区域（整个屏幕减去选择区域）
        QPainterPath path;
        path.addRect(rect());
        path.addRect(selectedArea);
        painter.drawPath(path);
        
        // 绘制选择区域边框
        painter.setPen(QPen(Qt::red, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selectedArea);
        
        // 绘制尺寸提示
        QString sizeText = QString("%1 x %2").arg(selectedArea.width()).arg(selectedArea.height());
        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(selectedArea.topLeft() + QPoint(5, -5), sizeText);
    } else {
        // 绘制半透明遮罩
        QColor maskColor(0, 0, 0, 120);
        painter.fillRect(rect(), maskColor);
        
        // 绘制提示文字
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
            // 开始选择区域
            m_isSelecting = true;
            m_startPoint = event->pos();
            m_endPoint = m_startPoint;
            m_rubberBand->setGeometry(QRect(m_startPoint, QSize()));
            m_rubberBand->show();
        } else {
            // 已选择区域，处理绘图操作
            if (m_currentMode != DrawMode::None) {
                m_startPoint = event->pos();
                m_endPoint = m_startPoint;
                
                // 如果点击在选择区域外，忽略事件
                if (!selectedRect().contains(m_startPoint)) {
                    return;
                }
                
                // 根据当前模式处理事件
                if (m_currentMode == DrawMode::Text) {
                    // 文字模式，弹出文本输入对话框
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
                    // 画笔模式，开始收集路径点
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
        // 更新选择区域
        m_endPoint = event->pos();
        m_rubberBand->setGeometry(selectedRect());
        update();
    } else if (m_hasSelected && m_currentMode != DrawMode::None && m_currentMode != DrawMode::Text) {
        // 绘制图形过程中
        if (event->buttons() & Qt::LeftButton) {
            m_endPoint = event->pos();
            
            // 如果是画笔模式，收集路径点
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
            // 完成选择区域
            m_isSelecting = false;
            m_hasSelected = true;
            m_endPoint = event->pos();
            
            // 显示工具栏
            QRect selectedArea = selectedRect();
            m_toolBar->move(selectedArea.right() - m_toolBar->width(), 
                            selectedArea.bottom() + 5);
            m_toolBar->setVisible(true);
            
            update();
        } else if (m_hasSelected && m_currentMode != DrawMode::None && m_currentMode != DrawMode::Text) {
            // 完成绘制图形
            m_endPoint = event->pos();
            
            // 如果在选择区域内绘制
            if (selectedRect().contains(m_startPoint)) {
                DrawItem item;
                item.mode = m_currentMode;
                item.start = m_startPoint;
                item.end = m_endPoint;
                item.rect = QRect(m_startPoint, m_endPoint).normalized();
                item.color = Qt::red;
                
                // 如果是画笔模式，保存收集的路径点
                if (m_currentMode == DrawMode::Brush && !m_currentBrushPoints.isEmpty()) {
                    item.brushPoints = m_currentBrushPoints;
                    // 清空当前路径点，为下一次绘制做准备
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
        // 按ESC取消截图
        cancelScreenshot();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // 按回车完成截图
        if (m_hasSelected) {
            finishScreenshot();
        }
    } else if (event->key() == Qt::Key_Z && event->modifiers() == Qt::ControlModifier) {
        // Ctrl+Z撤销操作
        undo();
    } else if (event->key() == Qt::Key_S && event->modifiers() == Qt::ControlModifier) {
        // Ctrl+S保存截图
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
    
    // 获取截图区域
    QRect selectedArea = selectedRect();
    
    // 创建截图
    QPixmap screenshot = m_screenPixmap.copy(selectedArea);
    
    // 在截图上绘制用户添加的图形
    QPainter painter(&screenshot);
    
    // 调整坐标，使相对于选择区域
    painter.translate(-selectedArea.topLeft());
    
    // 绘制用户添加的图形
    for (const DrawItem &item : m_drawItems) {
        // 仅绘制在选择区域内的图形
        if (selectedArea.contains(item.start)) {
            painter.setPen(QPen(item.color, 2));
            
            switch (item.mode) {
            case DrawMode::Rectangle:
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(QRect(item.start, item.end).normalized());
                break;
            case DrawMode::Circle:
                painter.setBrush(Qt::NoBrush);
                // 只绘制椭圆，不显示外接矩形框
                painter.drawEllipse(QRect(item.start, item.end).normalized());
                break;
            case DrawMode::Arrow: {
                // 只绘制线和箭头，不显示外接矩形框
                QLineF line(item.start, item.end);
                painter.drawLine(line);
                
                // 绘制箭头
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
                // 绘制画笔路径
                if (!item.brushPoints.isEmpty()) {
                    painter.setPen(QPen(item.color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    
                    // 如果只有一个点，绘制一个小圆点
                    if (item.brushPoints.size() == 1) {
                        painter.drawPoint(item.brushPoints.first());
                    } else {
                        // 绘制连接所有点的线段
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
    
    // 保存截图到文件
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
        // 获取截图区域
        QRect selectedArea = selectedRect();
        
        // 创建截图
        QPixmap screenshot = m_screenPixmap.copy(selectedArea);
        
        // 在截图上绘制用户添加的图形
        QPainter painter(&screenshot);
        
        // 调整坐标，使相对于选择区域
        painter.translate(-selectedArea.topLeft());
        
        // 绘制用户添加的图形
        for (const DrawItem &item : m_drawItems) {
            // 仅绘制在选择区域内的图形
            if (selectedArea.contains(item.start)) {
                painter.setPen(QPen(item.color, 2));
                
                switch (item.mode) {
                case DrawMode::Rectangle:
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(QRect(item.start, item.end).normalized());
                    break;
                case DrawMode::Circle:
                    painter.setBrush(Qt::NoBrush);
                    // 只绘制椭圆，不显示外接矩形框
                    painter.drawEllipse(QRect(item.start, item.end).normalized());
                    break;
                case DrawMode::Arrow: {
                    // 只绘制线和箭头，不显示外接矩形框
                    QLineF line(item.start, item.end);
                    painter.drawLine(line);
                    
                    // 绘制箭头
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
                    // 绘制画笔路径
                    if (!item.brushPoints.isEmpty()) {
                        painter.setPen(QPen(item.color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                        
                        // 如果只有一个点，绘制一个小圆点
                        if (item.brushPoints.size() == 1) {
                            painter.drawPoint(item.brushPoints.first());
                        } else {
                            // 绘制连接所有点的线段
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
        
        // 结束绘制器，确保所有绘制操作已完成
        painter.end();
        
        // 复制到剪贴板 - 尝试多种方法确保成功
        QClipboard *clipboard = QApplication::clipboard();
        if (clipboard) {
            // 方法1: 直接设置pixmap (标准方法)
            clipboard->setPixmap(screenshot);
            
            // 方法2: 作为图像设置 (备用方法)
            clipboard->setImage(screenshot.toImage());
            
            // 方法3: 创建MIME数据包(多格式支持)
            QMimeData *mimeData = new QMimeData();
            
            // 转换为PNG格式
            QByteArray pngData;
            QBuffer buffer(&pngData);
            buffer.open(QIODevice::WriteOnly);
            screenshot.save(&buffer, "PNG");
            buffer.close();
            
            // 设置多种数据格式
            mimeData->setData("image/png", pngData);
            mimeData->setImageData(screenshot.toImage());
            
            // 手动设置MIME数据
            clipboard->setMimeData(mimeData);
            
            // 强制X11平台的剪贴板同步
            #if defined(Q_OS_LINUX)
            QApplication::processEvents();
            #endif
            
            // 使用定时器等待剪贴板操作完成
            QTimer::singleShot(200, [this]() {
                QMessageBox::information(this, "截图完成", "截图已复制到剪贴板\n请使用Ctrl+V粘贴到目标应用程序");
                m_isScreenshotMode = false;
                hide();
            });
            
            // 不要立即隐藏窗口，等待定时器触发
            return;
        } else {
            QMessageBox::warning(this, "剪贴板错误", "无法访问系统剪贴板");
        }
    }
    
    // 如果没有选择区域或出现错误，隐藏窗口
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
    // 每次切换到画笔模式时清空当前的画笔点
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
    // 首先绘制已保存的图形项目
    for (const DrawItem &item : m_drawItems) {
        painter.setPen(QPen(item.color, 2));
        
        switch (item.mode) {
        case DrawMode::Rectangle:
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRect(item.start, item.end).normalized());
            break;
        case DrawMode::Circle:
            painter.setBrush(Qt::NoBrush);
            // 只绘制椭圆，不绘制外接矩形
            painter.drawEllipse(QRect(item.start, item.end).normalized());
            break;
        case DrawMode::Arrow: {
            // 只绘制箭头线和箭头头部，不绘制外接矩形
            QLineF line(item.start, item.end);
            painter.drawLine(line);
            
            // 绘制箭头
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
            // 绘制画笔路径
            if (!item.brushPoints.isEmpty()) {
                painter.setPen(QPen(item.color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                
                // 如果只有一个点，绘制一个小圆点
                if (item.brushPoints.size() == 1) {
                    painter.drawPoint(item.brushPoints.first());
                } else {
                    // 绘制连接所有点的线段
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
    
    // 绘制当前正在绘制的画笔路径（如果有）
    if (m_currentMode == DrawMode::Brush && !m_currentBrushPoints.isEmpty()) {
        painter.setPen(QPen(Qt::red, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        
        // 如果只有一个点，绘制一个小圆点
        if (m_currentBrushPoints.size() == 1) {
            painter.drawPoint(m_currentBrushPoints.first());
        } else {
            // 绘制连接所有点的线段
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