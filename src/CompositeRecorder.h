#pragma once

#include "GdiCompositeCapturer.h"
#include "WindowLayout.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

class CompositeRecorder : public QObject
{
    Q_OBJECT
public:
    explicit CompositeRecorder(QObject* parent = nullptr);
    ~CompositeRecorder() override;

    void setLayout(WindowLayoutManager* layout);

    bool isRecording() const { return recording_.load(); }

public slots:
    void startRecording(const QString& outputPath, int fps = 10);
    void stopRecording();

signals:
    void recordingStarted(const QString& path);
    void canvasResized(const QSize& canvasSize);
    void recordingStopped(const QString& path, int frameCount, const QSize& lastCanvasSize);
    void frameCaptured(int frameIndex);
    void errorOccurred(const QString& message);

private:
    struct BufferedFrame {
        QSize size;
        std::vector<uint8_t> bgra;
    };

    bool openAvi(const QString& path, int width, int height, int fps);
    bool writeAviFrame(const std::vector<uint8_t>& bgra, int contentWidth, int contentHeight);
    void closeAvi();
    bool flushBufferedFramesToAvi(const QString& aviPath);
    QString encodeToH264Mp4(const QString& aviPath, const QString& mp4Path);
    QString aviTempPathFor(const QString& finalPath) const;
    void recordLoop(int fps);
    void sleepUntilNextFrame(const std::chrono::steady_clock::time_point& frameStart, int fps);

    WindowLayoutManager* layout_ = nullptr;
    GdiCompositeCapturer capturer_;
    std::thread worker_;
    std::atomic_bool recording_{ false };
    std::atomic_bool userStopRequested_{ false };

    std::vector<BufferedFrame> bufferedFrames_;
    int recordFps_ = 10;

    void* aviFile_ = nullptr;
    void* aviStream_ = nullptr;
    long aviFrameIndex_ = 0;
    int aviWidth_ = 0;
    int aviHeight_ = 0;
    QString outputPath_;
};
