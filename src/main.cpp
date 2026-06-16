#include "MainWindow.h"
#include "WebViewWindow.h"
#include "WindowLayout.h"
#include "CompositeRecorder.h"
#include "GdiCompositeCapturer.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QLabel>
#include <QPixmap>
#include <QTimer>
#include <QtWebEngine/QtWebEngine>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr int kLayoutDebounceMs = 500;
constexpr int kPreviewStartDelayMs = 1000;

QUrl webViewUrl(int childId)
{
    switch (childId) {
    case 1: return QUrl(QStringLiteral("https://www.baidu.com"));
    case 2: return QUrl(QStringLiteral("https://www.bing.com"));
    case 3: return QUrl(QStringLiteral("https://github.com"));
    case 4: return QUrl(QStringLiteral("https://www.sohu.com"));
    case 5: return QUrl(QStringLiteral("https://www.sina.com.cn"));
    case 6: return QUrl(QStringLiteral("https://www.163.com"));
    default: return QUrl(QStringLiteral("https://www.baidu.com"));
    }
}

void placeWindowChain(HWND mainHwnd, const QVector<HWND>& children, int spacing)
{
    if (!mainHwnd)
        return;

    HWND prev = mainHwnd;
    for (HWND child : children) {
        if (!child || !IsWindow(child))
            continue;
        RECT prevRect{};
        GetWindowRect(prev, &prevRect);
        RECT childRect{};
        GetWindowRect(child, &childRect);
        const int w = childRect.right - childRect.left;
        const int h = childRect.bottom - childRect.top;
        const int x = prevRect.right + spacing;
        const int y = prevRect.top;
        SetWindowPos(child, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
        prev = child;
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QtWebEngine::initialize();

    QApplication app(argc, argv);

    WindowLayoutManager layoutManager;
    CompositeRecorder recorder;
    GdiCompositeCapturer previewCapturer;

    MainWindow mainWin;
    QVector<WebViewWindow*> childWins(DemoChildPreset::kMaxChildren, nullptr);
    QVector<bool> childOpen(DemoChildPreset::kMaxChildren, false);

    mainWin.show();

    const auto visibleChildren = [&]() -> QVector<HWND> {
        QVector<HWND> children;
        for (int i = 0; i < DemoChildPreset::kMaxChildren; ++i) {
            if (childOpen[i] && childWins[i])
                children.append(childWins[i]->nativeHandle());
        }
        return children;
    };

    const auto syncPendingLayout = [&]() {
        layoutManager.setMainWindow(mainWin.nativeHandle());
        for (int i = 0; i < DemoChildPreset::kMaxChildren; ++i) {
            const int childId = i + 1;
            HWND hwnd = nullptr;
            if (childOpen[i] && childWins[i])
                hwnd = childWins[i]->nativeHandle();
            layoutManager.setPendingChild(childId, hwnd);
        }
        placeWindowChain(mainWin.nativeHandle(), visibleChildren(), WindowLayoutManager::kDesktopChainSpacing);
    };

    const auto syncCanvasUi = [&]() {
        const QSize canvas = layoutManager.canvasSize();
        const bool layoutReady = canvas.isValid();
        const int count = layoutReady
            ? layoutManager.windowSlots().size()
            : layoutManager.pendingWindowCount();
        mainWin.setCanvasInfo(
            layoutReady ? canvas : QSize(WindowLayoutManager::kCanvasWidth, WindowLayoutManager::kCanvasHeight),
            count,
            layoutReady ? layoutManager.rowCount() : 0,
            layoutReady ? layoutManager.pageCount() : 0,
            layoutReady ? layoutManager.layoutSummary() : QStringLiteral("布局计算中…"));
    };

    const auto commitLayout = [&]() {
        layoutManager.commitPendingLayout();
        syncCanvasUi();
    };

    const auto scheduleLayoutSync = [&](int delayMs = 0) {
        QTimer::singleShot(delayMs, &app, [&]() {
            syncPendingLayout();
            commitLayout();
        });
    };

    QTimer layoutDebounce;
    layoutDebounce.setSingleShot(true);
    layoutDebounce.setInterval(kLayoutDebounceMs);
    QObject::connect(&layoutManager, &WindowLayoutManager::pendingChanged, &app, [&]() {
        syncCanvasUi();
        layoutDebounce.start();
    });
    QObject::connect(&layoutDebounce, &QTimer::timeout, &app, commitLayout);

    scheduleLayoutSync(0);
    scheduleLayoutSync(300);

    QLabel preview;
    preview.setWindowTitle(QStringLiteral("GDI 合成预览"));
    preview.setAlignment(Qt::AlignCenter);
    preview.setStyleSheet(QStringLiteral("background-color: #111; color: #888;"));
    preview.setText(QStringLiteral("等待布局就绪..."));

    QString previewLayoutKey;
    QTimer previewTimer;
    previewTimer.setInterval(200);

    const auto updatePreview = [&]() {
        const QString key = layoutManager.layoutKey();
        if (key.isEmpty())
            return;

        if (previewLayoutKey != key) {
            if (!previewCapturer.init(&layoutManager))
                return;
            previewLayoutKey = key;
        }

        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        std::vector<uint8_t> bgra;
        if (!previewCapturer.captureFrame(bgra))
            return;

        const QSize size = previewCapturer.canvasSize();
        QImage image(bgra.data(), size.width(), size.height(), size.width() * 4, QImage::Format_ARGB32);
        QPixmap pixmap = QPixmap::fromImage(image.copy());

        constexpr int kMaxPreviewWidth = 1280;
        double scale = 1.0;
        if (size.width() > kMaxPreviewWidth)
            scale = static_cast<double>(kMaxPreviewWidth) / size.width();

        const QSize displaySize(
            std::max(1, static_cast<int>(size.width() * scale)),
            std::max(1, static_cast<int>(size.height() * scale)));

        preview.setFixedSize(displaySize);
        preview.setPixmap(pixmap.scaled(displaySize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        preview.setText(QString());
    };

    const auto startPreview = [&]() {
        if (preview.isVisible())
            return;
        preview.show();
        preview.raise();
        updatePreview();
        previewTimer.start();
    };

    QObject::connect(&previewTimer, &QTimer::timeout, &app, updatePreview);
    QObject::connect(&layoutManager, &WindowLayoutManager::committed, &app, [&]() {
        syncCanvasUi();
        if (!preview.isVisible())
            return;
        previewLayoutKey.clear();
        updatePreview();
    });

    QTimer::singleShot(kPreviewStartDelayMs, &app, startPreview);

    recorder.setLayout(&layoutManager);

    QObject::connect(&mainWin, &MainWindow::toggleWebViewRequested, &app, [&](int childId) {
        if (childId < 1 || childId > DemoChildPreset::kMaxChildren)
            return;

        const int index = childId - 1;
        if (childOpen[index] && childWins[index]) {
            childWins[index]->close();
            childWins[index]->deleteLater();
            childWins[index] = nullptr;
            childOpen[index] = false;
            mainWin.setWebViewOpen(childId, false);
            scheduleLayoutSync(0);
            return;
        }

        const QString title = QStringLiteral("%1 WebView").arg(DemoChildPreset::label(childId));
        auto* win = new WebViewWindow(title, webViewUrl(childId));
        win->resize(DemoChildPreset::defaultSize(childId));
        childWins[index] = win;
        childOpen[index] = true;
        mainWin.setWebViewOpen(childId, true);
        win->show();
        win->raise();
        win->requestActivate();
        QObject::connect(win, &WebViewWindow::closed, &app, [&, childId, index]() {
            childOpen[index] = false;
            mainWin.setWebViewOpen(childId, false);
            childWins[index] = nullptr;
            scheduleLayoutSync(0);
        });
        scheduleLayoutSync(0);
        scheduleLayoutSync(200);
    });

    QObject::connect(&mainWin, &MainWindow::startRecordRequested, &app, [&]() {
        commitLayout();
        const QString fileName = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
        const QString outPath = QDir(app.applicationDirPath()).filePath(
            QStringLiteral("output/composite_%1.flv").arg(fileName));
        recorder.startRecording(outPath, 10);
    });

    QObject::connect(&mainWin, &MainWindow::stopRecordRequested, &app, [&]() {
        recorder.stopRecording();
    });

    QObject::connect(&recorder, &CompositeRecorder::canvasResized, &mainWin, [&](const QSize& size) {
        mainWin.setCanvasInfo(
            size,
            layoutManager.windowSlots().size(),
            layoutManager.rowCount(),
            layoutManager.pageCount(),
            layoutManager.layoutSummary());
        mainWin.setStatusText(
            QStringLiteral("录制中：画布已调整为 %1×%2").arg(size.width()).arg(size.height()));
    });

    QObject::connect(&recorder, &CompositeRecorder::recordingStopped, &mainWin,
        [&](const QString& path, int frames, const QSize& lastSize) {
            mainWin.setRecording(false);
            mainWin.setStatusText(
                QStringLiteral("状态：已保存 %1（%2 帧，末帧画布 %3×%4）")
                    .arg(path)
                    .arg(frames)
                    .arg(lastSize.width())
                    .arg(lastSize.height()));
        });

    QObject::connect(&recorder, &CompositeRecorder::errorOccurred, &mainWin, [&](const QString& msg) {
        mainWin.setRecording(false);
        mainWin.setStatusText(QStringLiteral("错误：%1").arg(msg));
    });

    return app.exec();
}
