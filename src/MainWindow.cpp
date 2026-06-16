#include "MainWindow.h"
#include "WindowLayout.h"

#include <QGridLayout>
#include <QGroupBox>
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
    setWindowTitle(QStringLiteral("multiRecordGdiDemo - 主窗 (6 WebView)"));
    resize(1200, 800);
    setMinimumSize(1200, 800);
    setAttribute(Qt::WA_NativeWindow);
    setStyleSheet(QStringLiteral("background-color: #1a1a2e; color: #eaeaea;"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* title = new QLabel(QStringLiteral("主窗口区域（模拟 yuer 主进程 QWidget）"), this);
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
    layout->addWidget(title);

    auto* hint = new QLabel(
        QStringLiteral("固定画布 1920×1080；仅主窗时 contain 居中填满；多窗时主窗 60%%、子窗均分 40%%。\n"
                       "子窗 >2 时：主窗槽位面积 60%%（等比 contain），前 2 子窗右上，其余第 2 排。\n"
                       "槽内 contain 居中，不裁剪。\n"
                       "子窗开关 500ms 防抖后重排槽位，录制不中断。停止后 ffmpeg 编码 H.264 FLV。"),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    canvasLabel_ = new QLabel(QStringLiteral("画布：-"), this);
    layout->addWidget(canvasLabel_);

    auto* childGroup = new QGroupBox(QStringLiteral("测试子窗（共 6 个 WebView）"), this);
    auto* childGrid = new QGridLayout(childGroup);
    childGrid->setHorizontalSpacing(8);
    childGrid->setVerticalSpacing(8);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int childId = row * 3 + col + 1;
            auto* btn = new QPushButton(
                QStringLiteral("打开 %1").arg(DemoChildPreset::label(childId)), childGroup);
            btn->setMinimumHeight(36);
            webViewBtns_[childId - 1] = btn;
            connect(btn, &QPushButton::clicked, this, [this, childId]() {
                emit toggleWebViewRequested(childId);
            });
            childGrid->addWidget(btn, row, col);
        }
    }
    layout->addWidget(childGroup);

    layout->addStretch(1);

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
    if (!self->testAttribute(Qt::WA_WState_Created))
        self->winId();
    return reinterpret_cast<HWND>(self->winId());
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
