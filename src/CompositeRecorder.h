#pragma once

#include "GdiCompositeCapturer.h"
#include "WindowLayout.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <atomic>
#include <thread>
#include <vector>

class QTimer;

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

    void captureTick();
    void finalizeRecording(QString outputPath, int fps, std::vector<BufferedFrame> frames);

    static bool openAvi(void** aviFile, void** aviStream, long* frameIndex, int* aviWidth, int* aviHeight,
        const QString& path, int width, int height, int fps);
    static void closeAvi(void** aviFile, void** aviStream);
    static bool writeAviFrame(void* aviStream, long* frameIndex, int aviWidth, int aviHeight,
        const std::vector<uint8_t>& bgra, int contentWidth, int contentHeight);
    static bool flushFramesToAvi(const QString& aviPath, int fps, const std::vector<BufferedFrame>& frames);
    static QString encodeToFlv(const QString& aviPath, const QString& flvPath, QString* errorOut);
    static QString aviTempPathFor(const QString& finalPath);

    WindowLayoutManager* layout_ = nullptr;
    GdiCompositeCapturer capturer_;
    QTimer* captureTimer_ = nullptr;
    std::thread finalizeThread_;

    std::atomic_bool recording_{ false };
    std::atomic_bool finalizing_{ false };

    std::vector<BufferedFrame> bufferedFrames_;
    QString activeLayoutKey_;
    int recordFps_ = 10;
    QString outputPath_;
};
