#include "CompositeRecorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <vfw.h>

#include <algorithm>
#include <cstring>

CompositeRecorder::CompositeRecorder(QObject* parent)
    : QObject(parent)
    , captureTimer_(new QTimer(this))
{
    captureTimer_->setTimerType(Qt::PreciseTimer);
    connect(captureTimer_, &QTimer::timeout, this, &CompositeRecorder::captureTick);
}

CompositeRecorder::~CompositeRecorder()
{
    captureTimer_->stop();
    recording_ = false;
    if (finalizeThread_.joinable())
        finalizeThread_.join();
    capturer_.shutdown();
}

void CompositeRecorder::setLayout(WindowLayoutManager* layout)
{
    layout_ = layout;
}

void CompositeRecorder::startRecording(const QString& outputPath, int fps)
{
    if (recording_.load() || finalizing_.load() || !layout_)
        return;

    if (finalizeThread_.joinable())
        finalizeThread_.join();

    QFileInfo info(outputPath);
    QDir().mkpath(info.absolutePath());

    outputPath_ = outputPath;
    recordFps_ = std::max(1, fps);
    bufferedFrames_.clear();
    activeLayoutKey_.clear();
    recording_ = true;

    if (!capturer_.init(layout_)) {
        recording_ = false;
        emit errorOccurred(QStringLiteral("GDI 合成采集器初始化失败"));
        return;
    }

    activeLayoutKey_ = layout_->layoutKey();
    captureTimer_->setInterval(1000 / recordFps_);
    captureTimer_->start();
    emit recordingStarted(outputPath);
}

void CompositeRecorder::stopRecording()
{
    if (!recording_.load() && !finalizing_.load())
        return;

    captureTimer_->stop();
    recording_ = false;
    capturer_.shutdown();

    if (bufferedFrames_.empty()) {
        emit recordingStopped(QString(), 0, QSize());
        return;
    }

    if (finalizing_.load())
        return;

    finalizing_ = true;
    const QString outputPath = outputPath_;
    const int fps = recordFps_;
    auto frames = std::move(bufferedFrames_);
    bufferedFrames_.clear();

    if (finalizeThread_.joinable())
        finalizeThread_.join();

    finalizeThread_ = std::thread([this, outputPath, fps, frames = std::move(frames)]() mutable {
        finalizeRecording(outputPath, fps, std::move(frames));
    });
}

void CompositeRecorder::captureTick()
{
    if (!recording_.load() || !layout_)
        return;

    const QString currentKey = layout_->layoutKey();
    if (currentKey != activeLayoutKey_) {
        if (capturer_.resizeToLayout()) {
            activeLayoutKey_ = currentKey;
            const QSize newSize = capturer_.canvasSize();
            emit canvasResized(newSize);
        }
    }

    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    std::vector<uint8_t> frame;
    if (!capturer_.captureFrame(frame)) {
        captureTimer_->stop();
        recording_ = false;
        capturer_.shutdown();
        emit errorOccurred(QStringLiteral("采集帧失败"));
        return;
    }

    BufferedFrame buffered;
    buffered.size = capturer_.canvasSize();
    buffered.bgra = std::move(frame);
    bufferedFrames_.push_back(std::move(buffered));
    emit frameCaptured(static_cast<int>(bufferedFrames_.size()));
}

void CompositeRecorder::finalizeRecording(QString outputPath, int fps, std::vector<BufferedFrame> frames)
{
    const int frameCount = static_cast<int>(frames.size());
    const QSize lastSize = frameCount > 0 ? frames.back().size : QSize();

    QString savedPath = outputPath;
    QString errorMessage;
    const QString aviPath = aviTempPathFor(outputPath);

    if (!flushFramesToAvi(aviPath, fps, frames)) {
        errorMessage = QStringLiteral("写入临时 AVI 失败");
        savedPath.clear();
    } else if (outputPath.endsWith(QStringLiteral(".flv"), Qt::CaseInsensitive)) {
        savedPath = encodeToFlv(aviPath, outputPath, &errorMessage);
    } else {
        savedPath = aviPath;
    }

    QMetaObject::invokeMethod(this, [this, savedPath, frameCount, lastSize, errorMessage]() {
        finalizing_ = false;
        if (!errorMessage.isEmpty())
            emit errorOccurred(errorMessage);
        emit recordingStopped(savedPath, frameCount, lastSize);
    }, Qt::QueuedConnection);
}

QString CompositeRecorder::aviTempPathFor(const QString& finalPath)
{
    if (finalPath.endsWith(QStringLiteral(".flv"), Qt::CaseInsensitive))
        return finalPath.left(finalPath.size() - 4) + QStringLiteral(".avi");
    return finalPath;
}

bool CompositeRecorder::openAvi(void** aviFile, void** aviStream, long* frameIndex, int* aviWidth, int* aviHeight,
    const QString& path, int width, int height, int fps)
{
    closeAvi(aviFile, aviStream);

    PAVIFILE file = nullptr;
    const HRESULT hr = AVIFileOpenW(&file, reinterpret_cast<LPCWSTR>(path.utf16()),
        OF_WRITE | OF_CREATE, nullptr);
    if (FAILED(hr) || !file)
        return false;

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
        return false;
    }

    *aviFile = file;
    *aviStream = stream;
    *frameIndex = 0;
    *aviWidth = width;
    *aviHeight = height;
    return true;
}

bool CompositeRecorder::writeAviFrame(void* aviStream, long* frameIndex, int aviWidth, int aviHeight,
    const std::vector<uint8_t>& bgra, int contentWidth, int contentHeight)
{
    if (!aviStream || contentWidth <= 0 || contentHeight <= 0)
        return false;

    const size_t expectedBytes = static_cast<size_t>(contentWidth) * contentHeight * 4;
    if (bgra.size() < expectedBytes || contentWidth > aviWidth || contentHeight > aviHeight)
        return false;

    const int srcStride = contentWidth * 4;
    std::vector<uint8_t> frameBuf(static_cast<size_t>(aviWidth) * aviHeight * 4, 0);
    const int dstStride = aviWidth * 4;

    for (int y = 0; y < contentHeight && y < aviHeight; ++y) {
        const size_t srcOffset = static_cast<size_t>(y) * srcStride;
        const size_t dstOffset = static_cast<size_t>(y) * dstStride;
        std::memcpy(frameBuf.data() + dstOffset, bgra.data() + srcOffset, srcStride);
    }

    const LONG written = AVIStreamWrite(static_cast<PAVISTREAM>(aviStream), *frameIndex, 1,
        frameBuf.data(), static_cast<LONG>(frameBuf.size()), AVIIF_KEYFRAME, nullptr, nullptr);
    if (written != AVIERR_OK)
        return false;

    ++(*frameIndex);
    return true;
}

void CompositeRecorder::closeAvi(void** aviFile, void** aviStream)
{
    if (aviStream && *aviStream) {
        AVIStreamRelease(static_cast<PAVISTREAM>(*aviStream));
        *aviStream = nullptr;
    }
    if (aviFile && *aviFile) {
        AVIFileRelease(static_cast<PAVIFILE>(*aviFile));
        *aviFile = nullptr;
    }
}

bool CompositeRecorder::flushFramesToAvi(const QString& aviPath, int fps, const std::vector<BufferedFrame>& frames)
{
    if (frames.empty())
        return true;

    const QSize firstSize = frames.front().size;
    const bool uniformSize = std::all_of(
        frames.begin(), frames.end(),
        [&](const BufferedFrame& frame) { return frame.size == firstSize; });

    int aviW = firstSize.width();
    int aviH = firstSize.height();
    if (!uniformSize) {
        for (const BufferedFrame& frame : frames) {
            aviW = std::max(aviW, frame.size.width());
            aviH = std::max(aviH, frame.size.height());
        }
    }

    void* aviFile = nullptr;
    void* aviStream = nullptr;
    long frameIndex = 0;
    int aviWidth = 0;
    int aviHeight = 0;

    AVIFileInit();

    bool ok = openAvi(&aviFile, &aviStream, &frameIndex, &aviWidth, &aviHeight, aviPath, aviW, aviH, fps);
    if (ok) {
        for (const BufferedFrame& frame : frames) {
            if (!writeAviFrame(aviStream, &frameIndex, aviWidth, aviHeight,
                    frame.bgra, frame.size.width(), frame.size.height())) {
                ok = false;
                break;
            }
        }
    }

    closeAvi(&aviFile, &aviStream);
    AVIFileExit();

    if (!ok)
        QFile::remove(aviPath);

    return ok;
}

QString CompositeRecorder::encodeToFlv(const QString& aviPath, const QString& flvPath, QString* errorOut)
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
        QStringLiteral("-an"),
        QStringLiteral("-f"), QStringLiteral("flv"),
        flvPath,
    });

    if (!ffmpeg.waitForStarted(5000)) {
        if (errorOut)
            *errorOut = QStringLiteral("未找到 ffmpeg，已保留未压缩 AVI：%1").arg(aviPath);
        return aviPath;
    }
    if (!ffmpeg.waitForFinished(-1) || ffmpeg.exitCode() != 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("FLV 编码失败，已保留 AVI：%1")
                .arg(QString::fromUtf8(ffmpeg.readAllStandardOutput()).trimmed());
        }
        return aviPath;
    }

    if (!QFile::remove(aviPath) && errorOut) {
        *errorOut = QStringLiteral("编码完成但无法删除临时 AVI：%1").arg(aviPath);
    }
    return flvPath;
}
