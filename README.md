
#### 说明

1.将在Windows上运行的截图工具改写到可以在Arch Linux上使用

2.目前基本截图功能已实现，可以保存文件、复制到剪贴板，但是编辑功能存在bug

3.计划后面实现贴图、长截图功能

#### 如何编译运行
1.编译
cd /home/xxx/Projects/Screenshot/build && ninja

2.运行
/home/xxx/Projects/Screenshot/build/ScreenshotLinux
此时会在系统托盘显示截图工具图标

#### 如何使用
1.第一次使用：编译 -> 运行，之后点击托盘图标

2.添加到开机自启：系统设置 -> 自动启动 -> 添加 -> 截图工具

3.此后开机后可以看到系统托盘有一个此截图工具

![alt text](image.png)
