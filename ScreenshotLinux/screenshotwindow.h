#ifndef SCREENSHOTWINDOW_H
#define SCREENSHOTWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QRubberBand>
#include <QToolBar>
#include <QAction>
#include <QSystemTrayIcon> // 添加系统托盘支持
#include <QMenu> // 添加菜单支持
#include <QVector> // 用于存储画笔路径点
#include <QPainterPath> // 用于画笔路径
#include <QRegion> // 用于创建遮罩区域

class ScreenshotWindow : public QWidget
{
    Q_OBJECT
    
public:
    ScreenshotWindow(QWidget *parent = nullptr);
    ~ScreenshotWindow();
    
    void setupTrayIcon(); // 设置系统托盘图标
    void startScreenshot(); // 开始截图过程
    
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override; // 处理关闭事件
    
private slots:
    void saveScreenshot();
    void cancelScreenshot();
    void finishScreenshot();
    void drawRectangle();
    void drawCircle();
    void drawArrow();
    void drawText();
    void drawBrush(); // 切换到画笔模式
    void undo();
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason); // 托盘图标点击响应
    void showAboutDialog(); // 显示关于对话框
    void quitApplication(); // 退出应用程序
    
private:
    enum class DrawMode {
        None,
        Rectangle,
        Circle,
        Arrow,
        Text,
        Brush,
        Mosaic
    };
    
    struct DrawItem {
        DrawMode mode;
        QRect rect;
        QPoint start;
        QPoint end;
        QString text;
        QColor color;
        QVector<QPoint> brushPoints; // 存储画笔路径上的点
    };
    
    void grabScreen();
    void drawOnPainter(QPainter &painter);
    QRect selectedRect() const;
    
    QPixmap m_screenPixmap;        // 全屏截图
    QPoint m_startPoint;           // 选择区域的起始点
    QPoint m_endPoint;             // 选择区域的结束点
    bool m_isSelecting;            // 是否正在选择区域
    bool m_hasSelected;            // 是否已经选择了区域
    bool m_isScreenshotMode;       // 是否处于截图模式
    
    DrawMode m_currentMode;        // 当前绘制模式
    QList<DrawItem> m_drawItems;   // 已绘制的图形项目
    QList<DrawItem> m_undoItems;   // 已撤销的图形项目
    
    QRubberBand *m_rubberBand;     // 橡皮筋选择框
    QToolBar *m_toolBar;           // 工具栏
    
    // 当前正在绘制的画笔路径点
    QVector<QPoint> m_currentBrushPoints;
    
    // 工具栏动作
    QAction *m_rectAction;
    QAction *m_circleAction;
    QAction *m_arrowAction;
    QAction *m_textAction;
    QAction *m_brushAction;
    QAction *m_mosaicAction;
    QAction *m_undoAction;
    QAction *m_saveAction;
    QAction *m_cancelAction;
    QAction *m_finishAction;
    
    // 系统托盘相关
    QSystemTrayIcon *m_trayIcon;   // 系统托盘图标
    QMenu *m_trayIconMenu;         // 托盘菜单
    QAction *m_screenshotAction;   // 截图动作
    QAction *m_aboutAction;        // 关于动作
    QAction *m_quitAction;         // 退出动作

    QRegion m_maskRegion;          // 遮罩区域，用于防止区域外点击
    
    void safeTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
};

#endif // SCREENSHOTWINDOW_H