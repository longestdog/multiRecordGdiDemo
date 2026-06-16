#include "GdiCompositeCapturer.h"

#include <algorithm>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

void fillBlack(HDC hdc, int width, int height)
{
    HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
    RECT rect{ 0, 0, width, height };
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

bool isChromeRenderClass(const wchar_t* className)
{
    return wcsstr(className, L"Chrome_RenderWidgetHostHWND") != nullptr
        || wcsstr(className, L"Chrome_WidgetWin") != nullptr;
}

int clientArea(HWND hwnd)
{
    RECT rect{};
    if (!GetClientRect(hwnd, &rect))
        return 0;
    const int w = static_cast<int>(rect.right - rect.left);
    const int h = static_cast<int>(rect.bottom - rect.top);
    return std::max(0, w) * std::max(0, h);
}

HWND findLargestChromeRenderHwnd(HWND parent, HWND best)
{
    for (HWND child = FindWindowExW(parent, nullptr, nullptr, nullptr);
         child != nullptr;
         child = FindWindowExW(parent, child, nullptr, nullptr)) {
        wchar_t className[256]{};
        GetClassNameW(child, className, 256);
        if (isChromeRenderClass(className) && clientArea(child) > clientArea(best))
            best = child;
        best = findLargestChromeRenderHwnd(child, best);
    }
    return best;
}

HWND resolveCaptureHwnd(HWND topLevel, bool* useClientRect)
{
    *useClientRect = false;
    if (!topLevel || !IsWindow(topLevel))
        return nullptr;

    const HWND chromeHwnd = findLargestChromeRenderHwnd(topLevel, nullptr);
    if (chromeHwnd && clientArea(chromeHwnd) > 0) {
        *useClientRect = true;
        return chromeHwnd;
    }
    return topLevel;
}

bool readCaptureSize(HWND hwnd, bool useClientRect, int& width, int& height)
{
    RECT rect{};
    if (useClientRect) {
        if (!GetClientRect(hwnd, &rect))
            return false;
    } else if (!GetWindowRect(hwnd, &rect)) {
        return false;
    }

    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    return width > 0 && height > 0;
}

} // namespace

bool GdiCompositeCapturer::ensureBitmap(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (inited_ && canvasSize_.width() == width && canvasSize_.height() == height)
        return true;

    HDC screenDc = GetDC(nullptr);
    if (!screenDc)
        return false;

    HDC newDc = CreateCompatibleDC(screenDc);
    HBITMAP newBmp = CreateCompatibleBitmap(screenDc, width, height);
    ReleaseDC(nullptr, screenDc);

    if (!newDc || !newBmp) {
        if (newBmp)
            DeleteObject(newBmp);
        if (newDc)
            DeleteDC(newDc);
        return false;
    }

    HBITMAP newOld = static_cast<HBITMAP>(SelectObject(newDc, newBmp));

    if (canvasDc_ && canvasBmpOld_)
        SelectObject(canvasDc_, canvasBmpOld_);
    if (canvasBmp_)
        DeleteObject(canvasBmp_);
    if (canvasDc_)
        DeleteDC(canvasDc_);

    canvasDc_ = newDc;
    canvasBmp_ = newBmp;
    canvasBmpOld_ = newOld;
    canvasSize_ = QSize(width, height);
    buffer_.assign(static_cast<size_t>(width) * height * 4, 0);
    inited_ = true;
    return true;
}

bool GdiCompositeCapturer::init(const WindowLayoutManager* layout)
{
    shutdown();
    if (!layout)
        return false;

    layout_ = layout;
    layoutKey_ = layout_->layoutKey();

    const QSize size = layout_->canvasSize();
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return false;

    return ensureBitmap(size.width(), size.height());
}

bool GdiCompositeCapturer::resizeToLayout()
{
    if (!layout_)
        return false;

    const QSize size = layout_->canvasSize();
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return false;

    layoutKey_ = layout_->layoutKey();
    return ensureBitmap(size.width(), size.height());
}

void GdiCompositeCapturer::shutdown()
{
    if (canvasDc_ && canvasBmpOld_)
        SelectObject(canvasDc_, canvasBmpOld_);

    if (canvasBmp_)
        DeleteObject(canvasBmp_);
    if (canvasDc_)
        DeleteDC(canvasDc_);

    canvasBmpOld_ = nullptr;
    canvasBmp_ = nullptr;
    canvasDc_ = nullptr;
    layout_ = nullptr;
    layoutKey_.clear();
    inited_ = false;
    buffer_.clear();
    canvasSize_ = QSize();
}

bool GdiCompositeCapturer::captureWindowToSlot(HDC canvasDc, const DemoWindowSlot& slot, int canvasWidth, int canvasHeight)
{
    Q_UNUSED(canvasWidth);
    Q_UNUSED(canvasHeight);

    if (!slot.hwnd || slot.slotWidth <= 0 || slot.slotHeight <= 0)
        return false;

    bool useClientRect = false;
    const HWND captureHwnd = resolveCaptureHwnd(slot.hwnd, &useClientRect);
    if (!captureHwnd)
        return false;

    int srcW = 0;
    int srcH = 0;
    if (!readCaptureSize(captureHwnd, useClientRect, srcW, srcH))
        return false;

    HDC screenDc = GetDC(nullptr);
    if (!screenDc)
        return false;

    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP memBmp = CreateCompatibleBitmap(screenDc, srcW, srcH);
    ReleaseDC(nullptr, screenDc);

    if (!memDc || !memBmp) {
        if (memBmp)
            DeleteObject(memBmp);
        if (memDc)
            DeleteDC(memDc);
        return false;
    }

    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDc, memBmp));
    fillBlack(memDc, srcW, srcH);

    bool captured = PrintWindow(captureHwnd, memDc, PW_RENDERFULLCONTENT) != FALSE;
    if (!captured) {
        HDC wndDc = GetDC(captureHwnd);
        if (wndDc) {
            captured = BitBlt(memDc, 0, 0, srcW, srcH, wndDc, 0, 0, SRCCOPY | CAPTUREBLT) != FALSE;
            ReleaseDC(captureHwnd, wndDc);
        }
    }
    if (!captured) {
        HDC wndDc = GetWindowDC(slot.hwnd);
        if (wndDc) {
            RECT windowRect{};
            GetWindowRect(slot.hwnd, &windowRect);
            const int fullW = windowRect.right - windowRect.left;
            const int fullH = windowRect.bottom - windowRect.top;
            if (fullW > 0 && fullH > 0) {
                captured = BitBlt(memDc, 0, 0, srcW, srcH, wndDc, 0, 0, SRCCOPY | CAPTUREBLT) != FALSE;
            }
            ReleaseDC(slot.hwnd, wndDc);
        }
    }

    if (!captured) {
        SelectObject(memDc, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDc);
        return false;
    }

    const int slotX = slot.slotX;
    const int slotY = slot.slotY;
    const int slotW = slot.slotWidth;
    const int slotH = slot.slotHeight;

    const double scale = std::min(static_cast<double>(slotW) / srcW,
        static_cast<double>(slotH) / srcH);
    const int drawW = std::max(1, static_cast<int>(srcW * scale + 0.5));
    const int drawH = std::max(1, static_cast<int>(srcH * scale + 0.5));
    const int drawX = slotX + (slotW - drawW) / 2;
    const int drawY = slotY + (slotH - drawH) / 2;

    SetStretchBltMode(canvasDc, HALFTONE);
    StretchBlt(canvasDc, drawX, drawY, drawW, drawH, memDc, 0, 0, srcW, srcH, SRCCOPY);

    SelectObject(memDc, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDc);
    return true;
}

bool GdiCompositeCapturer::captureFrame(std::vector<uint8_t>& bgraOut)
{
    if (!inited_ || !canvasDc_ || !layout_)
        return false;

    const int w = canvasSize_.width();
    const int h = canvasSize_.height();
    fillBlack(canvasDc_, w, h);

    const QVector<DemoWindowSlot> windowSlots = layout_->windowSlots();
    for (const DemoWindowSlot& slot : windowSlots)
        captureWindowToSlot(canvasDc_, slot, w, h);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = -h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    bgraOut.resize(static_cast<size_t>(w) * h * 4);
    if (GetDIBits(canvasDc_, canvasBmp_, 0, h, bgraOut.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS) <= 0)
        return false;

    buffer_ = bgraOut;
    return true;
}
