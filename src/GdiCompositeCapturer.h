#pragma once

#include "WindowLayout.h"

#include <QSize>
#include <QString>
#include <cstdint>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class GdiCompositeCapturer
{
public:
    bool init(const WindowLayoutManager* layout);
    bool resizeToLayout();
    void shutdown();

    QSize canvasSize() const { return canvasSize_; }

    bool captureFrame(std::vector<uint8_t>& bgraOut);

private:
    bool ensureBitmap(int width, int height);
    bool captureWindowToSlot(HDC canvasDc, const DemoWindowSlot& slot, int canvasWidth, int canvasHeight);

    const WindowLayoutManager* layout_ = nullptr;
    QSize canvasSize_;
    QString layoutKey_;
    std::vector<uint8_t> buffer_;
    HDC canvasDc_ = nullptr;
    HBITMAP canvasBmp_ = nullptr;
    HBITMAP canvasBmpOld_ = nullptr;
    bool inited_ = false;
};
