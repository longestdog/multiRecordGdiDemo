#include "LayoutSolver.h"

#include <algorithm>
#include <cmath>

namespace {

struct WorkingWindow : LayoutWindowIn {
    int row = 0;
    int slotX = 0;
    int slotY = 0;
    int slotW = 0;
    int slotH = 0;
};

QVector<int> splitWidth(int totalWidth, int count)
{
    QVector<int> widths;
    if (count <= 0)
        return widths;

    widths.resize(count);
    const int base = totalWidth / count;
    int remainder = totalWidth % count;
    for (int i = 0; i < count; ++i) {
        widths[i] = base + (i < remainder ? 1 : 0);
    }
    return widths;
}

LayoutResult makeResult(const QVector<WorkingWindow>& windows, int cw, int ch)
{
    LayoutResult result;
    result.canvasW = cw;
    result.canvasH = ch;
    result.ok = true;
    result.pageIndex = 0;
    result.pageCount = 1;
    result.rowCount = 1;

    for (const WorkingWindow& w : windows) {
        LayoutWindowOut out;
        static_cast<LayoutWindowIn&>(out) = static_cast<const LayoutWindowIn&>(w);
        out.row = w.row;
        out.slotX = w.slotX;
        out.slotY = w.slotY;
        out.slotW = w.slotW;
        out.slotH = w.slotH;
        LayoutSolver::containInSlot(
            w.srcW, w.srcH, w.slotX, w.slotY, w.slotW, w.slotH,
            out.drawX, out.drawY, out.drawW, out.drawH, out.scale);
        result.windows.append(out);
    }
    return result;
}

} // namespace

namespace LayoutSolver {

void containInSlot(int srcW, int srcH, int slotX, int slotY, int slotW, int slotH,
    int& drawX, int& drawY, int& drawW, int& drawH, float& scale)
{
    if (srcW <= 0 || srcH <= 0 || slotW <= 0 || slotH <= 0) {
        scale = 0.f;
        drawX = drawY = drawW = drawH = 0;
        return;
    }
    scale = static_cast<float>(std::min(
        static_cast<double>(slotW) / srcW,
        static_cast<double>(slotH) / srcH));
    drawW = std::max(1, static_cast<int>(srcW * scale + 0.5));
    drawH = std::max(1, static_cast<int>(srcH * scale + 0.5));
    drawX = slotX + (slotW - drawW) / 2;
    drawY = slotY + (slotH - drawH) / 2;
}

LayoutResult compute(const QVector<LayoutWindowIn>& windows, const LayoutConfig& config)
{
    LayoutResult empty;
    empty.ok = false;
    if (windows.isEmpty())
        return empty;

    QVector<LayoutWindowIn> sorted = windows;
    std::sort(sorted.begin(), sorted.end(), [](const LayoutWindowIn& a, const LayoutWindowIn& b) {
        return a.priority < b.priority;
    });

    if (sorted.front().priority != 0)
        return empty;

    const int cw = config.canvasW;
    const int ch = config.canvasH;

    QVector<WorkingWindow> working;
    working.reserve(sorted.size());
    for (const LayoutWindowIn& in : sorted) {
        WorkingWindow w;
        static_cast<LayoutWindowIn&>(w) = in;
        working.append(w);
    }

    WorkingWindow& mainWin = working.front();
    const int childCount = working.size() - 1;

    if (childCount <= 0) {
        mainWin.row = 0;
        mainWin.slotX = 0;
        mainWin.slotY = 0;
        mainWin.slotW = cw;
        mainWin.slotH = ch;
        return makeResult(working, cw, ch);
    }

    const int mainW = static_cast<int>(std::round(cw * config.mainWidthRatio));
    const int childTotalW = cw - mainW;
    const QVector<int> childWidths = splitWidth(childTotalW, childCount);

    mainWin.row = 0;
    mainWin.slotX = 0;
    mainWin.slotY = 0;
    mainWin.slotW = mainW;
    mainWin.slotH = ch;

    int x = mainW + config.slotGap;
    for (int i = 0; i < childCount; ++i) {
        WorkingWindow& child = working[i + 1];
        child.row = 0;
        child.slotX = x;
        child.slotY = 0;
        child.slotW = childWidths[i];
        child.slotH = ch;
        x += child.slotW + config.slotGap;
    }

    return makeResult(working, cw, ch);
}

} // namespace LayoutSolver
