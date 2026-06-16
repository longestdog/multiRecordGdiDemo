#include "MainWindow.h"
#include "WindowLayout.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("multiRecordGdiDemo - 主窗 (QWidget)"));
    resize(640, 480);
    setAttribute(Qt::WA_NativeWindow);
    setStyleSheet(QStringLiteral("background-color: #1a1a2e; color: #eaeaea;"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* title = new QLabel(QStringLiteral("主窗口区域（模拟 yuer 主进程 QWidget）"), this);
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
    layout->addWidget(title);

    auto* hint = new QLabel(
        QStringLiteral("固定画布 1920×1080；LayoutSolver 按 minScale/priority 自动排版（单行→多行→分页）。\n"
                       "合成 contain 完整显示、不裁剪；slotGap=0。\n"
                       "子窗开关 500ms 防抖后重排槽位，录制不中断。停止后 ffmpeg 编码 H.264 MP4。"),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    canvasLabel_ = new QLabel(QStringLiteral("画布：-"), this);
    layout->addWidget(canvasLabel_);

    for (int row = 0; row < 2; ++row) {
        auto* childRow = new QHBoxLayout();
        for (int col = 0; col < 3; ++col) {
            const int childId = row * 3 + col + 1;
            auto* btn = new QPushButton(
                QStringLiteral("打开 %1").arg(DemoChildPreset::label(childId)), this);
            webViewBtns_[childId - 1] = btn;
            connect(btn, &QPushButton::clicked, this, [this, childId]() {
                emit toggleWebViewRequested(childId);
            });
            childRow->addWidget(btn);
        }
        layout->addLayout(childRow);
    }

    layout->addStretch();

    statusLabel_ = new QLabel(QStringLiteral("状态：就绪"), this);
    layout->addWidget(statusLabel_);

    recordBtn_ = new QPushButton(QStringLiteral("开始录制"), this);
    connect(recordBtn_, &QPushButton::clicked, this, [this]() {
        if (!recording_) {
            setRecording(true);
            emit startRecordRequested();
        } else {
            setRecording(false);
            statusLabel_->setText(QStringLiteral("状态：正在保存..."));
            emit stopRecordRequested();
        }
    });
    layout->addWidget(recordBtn_);
}

HWND MainWindow::nativeHandle() const
{
#ifdef Q_OS_WIN
    auto* self = const_cast<MainWindow*>(this);
    if (self->windowHandle())
        return reinterpret_cast<HWND>(self->winId());
    return nullptr;
#else
    return nullptr;
#endif
}

void MainWindow::setStatusText(const QString& text)
{
    if (statusLabel_)
        statusLabel_->setText(text);
}

void MainWindow::setCanvasInfo(const QSize& canvasSize, int windowCount, int rowCount, int pageCount, const QString& layoutSummary)
{
    if (!canvasLabel_)
        return;
    if (!canvasSize.isValid()) {
        canvasLabel_->setText(QStringLiteral("画布：-"));
        return;
    }

    QString text = QStringLiteral("画布：%1 × %2（%3 个窗口")
        .arg(canvasSize.width())
        .arg(canvasSize.height())
        .arg(windowCount);
    if (rowCount > 0)
        text += QStringLiteral("，%1 行").arg(rowCount);
    if (pageCount > 0)
        text += QStringLiteral("，1/%1 页").arg(pageCount);
    text += QLatin1Char(')');
    if (!layoutSummary.isEmpty())
        text += QStringLiteral("\n排版：%1").arg(layoutSummary);
    canvasLabel_->setText(text);
}

void MainWindow::setRecording(bool recording)
{
    recording_ = recording;
    if (recordBtn_)
        recordBtn_->setText(recording ? QStringLiteral("停止录制") : QStringLiteral("开始录制"));
    if (recording && statusLabel_)
        statusLabel_->setText(QStringLiteral("状态：录制中（子窗变化 500ms 后自动调整画布）..."));
}

void MainWindow::setWebViewOpen(int childId, bool open)
{
    if (childId < 1 || childId > DemoChildPreset::kMaxChildren)
        return;
    QPushButton* btn = webViewBtns_[childId - 1];
    if (!btn)
        return;
    btn->setText(open
        ? QStringLiteral("关闭 %1").arg(DemoChildPreset::label(childId))
        : QStringLiteral("打开 %1").arg(DemoChildPreset::label(childId)));
}
