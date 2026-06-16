# multiRecordGdiDemo

模拟 yuer-window-qt-4 多窗口 GDI 录制方案的独立 Demo。

## 功能

- **主窗**：`QWidget` 空白深色界面（模拟主进程主窗口）
- **子窗**：2 个 `QQuickView` + `Qt WebEngine`（模拟跨房连麦 / 小游戏）
- **采集**：`GDI + PrintWindow(PW_RENDERFULLCONTENT)` 将 3 个 HWND 并排合成到黑底画布（8px 间距）
- **预览**：独立窗口实时显示合成画面
- **录制**：单文件 AVI，画布宽度动态变化；文件以最大可能宽度存储，有效内容左对齐
- **子窗**：主窗按钮可动态打开/关闭跨房连麦、小游戏 WebView（500ms 防抖后更新画布）

## 依赖

- Qt 5.14.2 msvc2017_64（含 WebEngine）
- CMake 3.16+
- Visual Studio 2017/2019（v141 工具集）

## 编译

```bat
cd E:\Code\multiRecordGdiDemo
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH=D:/Qt/Qt5.14.2/5.14.2/msvc2017_64
cmake --build . --config Release
```

## 运行

```bat
D:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\windeployqt.exe --release --qmldir E:\Code\multiRecordGdiDemo\qml build\Release\multiRecordGdiDemo.exe
build\Release\multiRecordGdiDemo.exe
```

## 目录结构

```
multiRecordGdiDemo/
  CMakeLists.txt
  src/
    GdiCompositeCapturer.*   # GDI 多窗合成采集
    WindowLayout.*           # HWND 布局 / 画布尺寸
    CompositeRecorder.*      # 录制线程 + AVI 写出
    MainWindow.*             # QWidget 主窗
    WebViewWindow.*          # WebEngine 子窗
    main.cpp
  qml/WebPage.qml
  resources/resources.qrc
```
