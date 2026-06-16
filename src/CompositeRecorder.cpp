#include "CompositeRecorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <vfw.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

CompositeRecorder::CompositeRecorder(QObject* parent)
    : QObject(parent)
{
}

CompositeRecorder::~CompositeRecorder()
{
    stopRecording();
}

void CompositeRecorder::setLayout(WindowLayoutManager* layout)
{
    layout_ = layout;
}

void CompositeRecorder::startRecording(const QString& outputPath, int fps)
{
    if (recording_.load() || !layout_)
        return;

    QFileInfo info(outputPath);
    QDir().mkpath(info.absolutePath());

    outputPath_ = outputPath;
    recordFps_ = fps;
    aviFrameIndex_ = 0;
    bufferedFrames_.clear();
    userStopRequested_ = false;
    recording_ = true;

    worker_ = std::thread(&CompositeRecorder::recordLoop, this, fps);
    emit recordingStarted(outputPath);
}

void CompositeRecorder::stopRecording()
{
    if (!recording_.load())
        return;

    userStopRequested_ = true;
    if (worker_.joinable())
        worker_.join();

    flushBufferedFramesToAvi(aviTempPathFor(outputPath_));

    QString savedPath = outputPath_;
    const QString aviPath = aviTempPathFor(outputPath_);
    if (QFile::exists(aviPath)) {
        if (outputPath_.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
            savedPath = encodeToH264Mp4(aviPath, outputPath_);
        else
            savedPath = aviPath;
    }

    const int frameCount = static_cast<int>(bufferedFrames_.size());
    const QSize lastSize = frameCount > 0
        ? bufferedFrames_.back().size
        : capturer_.canvasSize();

    closeAvi();
    capturer_.shutdown();
    bufferedFrames_.clear();
    recording_ = false;

    emit recordingStopped(savedPath, frameCount, lastSize);
}

bool CompositeRecorder::openAvi(const QString& path, int width, int height, int fps)
{
    closeAvi();

    PAVIFILE file = nullptr;
    const HRESULT hr = AVIFileOpenW(&file, reinterpret_cast<LPCWSTR>(path.utf16()),
        OF_WRITE | OF_CREATE, nullptr);
    if (FAILED(hr) || !file) {
        emit errorOccurred(QStringLiteral("AVIFileOpen failed: %1").arg(hr));
        return false;
    }

    AVISTREAMINFO streamInfo{};
    streamInfo.fccType = streamtypeVIDEO;
    streamInfo.fccHandler = mmioFOURCC('D', 'I', 'B', ' ');
    streamInfo.dwScale = 1;
    streamInfo.dwRate = static_cast<DWORD>(std::max(1, fps));
    streamInfo.dwSuggestedBufferSize = width * height * 4;
    streamInfo.rcFrame.right = width;
    streamInfo.rcFrame.bottom = height;

    PAVISTREAM stream = nullptr;
    if (AVIFileCreateStream(file, &stream, &streamInfo) != AVIERR_OK) {
        AVIFileRelease(file);
        emit errorOccurred(QStringLiteral("AVIFileCreateStream failed"));
        return false;
    }

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width;
    bih.biHeight = -height;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = width * height * 4;

    if (AVIStreamSetFormat(stream, 0, &bih, sizeof(bih)) != AVIERR_OK) {
        AVIStreamRelease(stream);
        AVIFileRelease(file);
        emit errorOccurred(QStringLiteral("AVIStreamSetFormat failed"));
        return false;
    }

    aviFile_ = file;
    aviStream_ = stream;
    aviWidth_ = width;
    aviHeight_ = height;
    aviFrameIndex_ = 0;
    return true;
}

bool CompositeRecorder::writeAviFrame(const std::vector<uint8_t>& bgra, int contentWidth, int contentHeight)
{
    if (!aviFile_ || !aviStream_)
        return false;

    if (contentWidth <= 0 || contentHeight <= 0)
        return false;

    const size_t expectedBytes = static_cast<size_t>(contentWidth) * contentHeight * 4;
    if (bgra.size() < expectedBytes) {
        emit errorOccurred(QStringLiteral("帧缓冲区尺寸不匹配"));
        return false;
    }

    if (contentWidth > aviWidth_ || contentHeight > aviHeight_) {
        emit errorOccurred(
            QStringLiteral("画布超出 AVI 容量：内容 %1×%2，AVI %3×%4")
                .arg(contentWidth).arg(contentHeight).arg(aviWidth_).arg(aviHeight_));
        return false;
    }

    const int srcStride = contentWidth * 4;
    std::vector<uint8_t> frameBuf(static_cast<size_t>(aviWidth_) * aviHeight_ * 4, 0);
    const int dstStride = aviWidth_ * 4;

    for (int y = 0; y < contentHeight && y < aviHeight_; ++y) {
        const size_t srcOffset = static_cast<size_t>(y) * srcStride;
        const size_t dstOffset = static_cast<size_t>(y) * dstStride;
        std::memcpy(frameBuf.data() + dstOffset, bgra.data() + srcOffset, srcStride);
    }

    PAVISTREAM stream = static_cast<PAVISTREAM>(aviStream_);
    const LONG written = AVIStreamWrite(stream, aviFrameIndex_, 1,
        frameBuf.data(), static_cast<LONG>(frameBuf.size()), AVIIF_KEYFRAME, nullptr, nullptr);
    if (written != AVIERR_OK) {
        emit errorOccurred(QStringLiteral("AVIStreamWrite failed: %1").arg(written));
        return false;
    }

    ++aviFrameIndex_;
    return true;
}

bool CompositeRecorder::flushBufferedFramesToAvi(const QString& aviPath)
{
    if (bufferedFrames_.empty())
        return true;

    const QSize firstSize = bufferedFrames_.front().size;
    const bool uniformSize = std::all_of(
        bufferedFrames_.begin(), bufferedFrames_.end(),
        [&](const BufferedFrame& frame) { return frame.size == firstSize; });

    int aviW = firstSize.width();
    int aviH = firstSize.height();
    if (!uniformSize) {
        for (const BufferedFrame& frame : bufferedFrames_) {
            aviW = std::max(aviW, frame.size.width());
            aviH = std::max(aviH, frame.size.height());
        }
    }

    AVIFileInit();

    bool ok = openAvi(aviPath, aviW, aviH, recordFps_);
    if (ok) {
        for (const BufferedFrame& frame : bufferedFrames_) {
            if (!writeAviFrame(frame.bgra, frame.size.width(), frame.size.height())) {
                ok = false;
                break;
            }
        }
    }

    closeAvi();
    AVIFileExit();

    if (!ok)
        QFile::remove(aviPath);

    return ok;
}

QString CompositeRecorder::aviTempPathFor(const QString& finalPath) const
{
    if (finalPath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive))
        return finalPath.left(finalPath.size() - 4) + QStringLiteral(".avi");
    return finalPath;
}

QString CompositeRecorder::encodeToH264Mp4(const QString& aviPath, const QString& mp4Path)
{
    QString ffmpegPath = QCoreApplication::applicationDirPath() + QStringLiteral("/ffmpeg.exe");
    if (!QFile::exists(ffmpegPath))
        ffmpegPath = QStringLiteral("ffmpeg");

    QProcess ffmpeg;
    ffmpeg.setProcessChannelMode(QProcess::MergedChannels);
    ffmpeg.start(ffmpegPath, {
        QStringLiteral("-y"),
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-i"), aviPath,
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), QStringLiteral("fast"),
        QStringLiteral("-crf"), QStringLiteral("23"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        mp4Path,
    });

    if (!ffmpeg.waitForStarted(5000)) {
        emit errorOccurred(QStringLiteral("未找到 ffmpeg，已保留未压缩 AVI：%1").arg(aviPath));
        return aviPath;
    }
    if (!ffmpeg.waitForFinished(-1) || ffmpeg.exitCode() != 0) {
        const QString err = QString::fromUtf8(ffmpeg.readAllStandardOutput());
        emit errorOccurred(QStringLiteral("H.264 编码失败，已保留 AVI：%1").arg(err.trimmed()));
        return aviPath;
    }

    if (!QFile::remove(aviPath)) {
        emit errorOccurred(QStringLiteral("编码完成但无法删除临时 AVI：%1").arg(aviPath));
    }
    return mp4Path;
}

void CompositeRecorder::closeAvi()
{
    if (aviStream_) {
        AVIStreamRelease(static_cast<PAVISTREAM>(aviStream_));
        aviStream_ = nullptr;
    }
    if (aviFile_) {
        AVIFileRelease(static_cast<PAVIFILE>(aviFile_));
        aviFile_ = nullptr;
    }
}

void CompositeRecorder::sleepUntilNextFrame(const std::chrono::steady_clock::time_point& frameStart, int fps)
{
    const auto frameDuration = std::chrono::milliseconds(1000 / std::max(1, fps));
    const auto nextFrame = frameStart + frameDuration;
    const auto now = std::chrono::steady_clock::now();
    if (now < nextFrame)
        std::this_thread::sleep_until(nextFrame);
}

void CompositeRecorder::recordLoop(int fps)
{
    if (!capturer_.init(layout_)) {
        QMetaObject::invokeMethod(this, [this]() {
            emit errorOccurred(QStringLiteral("GDI 合成采集器初始化失败"));
        }, Qt::QueuedConnection);
        recording_ = false;
        return;
    }

    QString activeLayoutKey = layout_->layoutKey();
    int frameCount = 0;

    while (!userStopRequested_.load()) {
        const auto frameStart = std::chrono::steady_clock::now();

        const QString currentKey = layout_->layoutKey();
        if (currentKey != activeLayoutKey) {
            if (capturer_.resizeToLayout()) {
                activeLayoutKey = currentKey;
                const QSize newSize = capturer_.canvasSize();
                QMetaObject::invokeMethod(this, [this, newSize]() {
                    emit canvasResized(newSize);
                }, Qt::QueuedConnection);
            }
        }

        std::vector<uint8_t> frame;
        if (!capturer_.captureFrame(frame)) {
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred(QStringLiteral("采集帧失败"));
            }, Qt::QueuedConnection);
            break;
        }

        const QSize contentSize = capturer_.canvasSize();
        BufferedFrame buffered;
        buffered.size = contentSize;
        buffered.bgra = std::move(frame);
        bufferedFrames_.push_back(std::move(buffered));

        ++frameCount;
        const int currentFrame = frameCount;
        QMetaObject::invokeMethod(this, [this, currentFrame]() {
            emit frameCaptured(currentFrame);
        }, Qt::QueuedConnection);

        sleepUntilNextFrame(frameStart, fps);
    }

    recording_ = false;
}
