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

void flushRowToRight(QVector<WorkingWindow*>& rowSlots, int rowRight)
{
    if (rowSlots.isEmpty())
        return;
    WorkingWindow* last = rowSlots.back();
    const int rowEnd = last->slotX + last->slotW;
    if (rowEnd < rowRight)
        last->slotW += rowRight - rowEnd;
}

void flushRowToBottom(QVector<WorkingWindow*>& rowSlots, int rowBottom)
{
    if (rowSlots.isEmpty())
        return;
    const int rowY = rowSlots.front()->slotY;
    const int currentBottom = rowY + rowSlots.front()->slotH;
    if (currentBottom < rowBottom) {
        const int extra = rowBottom - currentBottom;
        for (WorkingWindow* slot : rowSlots)
            slot->slotH += extra;
    }
}

int mainSlotWidthForArea(int canvasW, int canvasH, int rowH, float mainAreaRatio)
{
    const int targetArea = static_cast<int>(std::ceil(mainAreaRatio * canvasW * canvasH));
    return std::max(1, std::min(canvasW, (targetArea + rowH - 1) / std::max(1, rowH)));
}

LayoutResult makeResult(const QVector<WorkingWindow>& windows, int cw, int ch)
{
    LayoutResult result;
    result.canvasW = cw;
    result.canvasH = ch;
    result.ok = true;
    result.pageIndex = 0;
    result.pageCount = 1;

    int maxRow = 0;
    for (const WorkingWindow& w : windows)
        maxRow = std::max(maxRow, w.row);
    result.rowCount = maxRow + 1;

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

    const int mainW = (cw * static_cast<int>(config.mainWidthRatio * 100 + 0.5f)) / 100;

    if (childCount <= 2) {
        const int childTotalW = cw - mainW;
        const QVector<int> childWidths = splitWidth(childTotalW, childCount);

        mainWin.row = 0;
        mainWin.slotX = 0;
        mainWin.slotY = 0;
        mainWin.slotW = mainW;
        mainWin.slotH = ch;

        QVector<WorkingWindow*> rowSlots;
        rowSlots.append(&mainWin);

        int x = mainW;
        for (int i = 0; i < childCount; ++i) {
            WorkingWindow& child = working[i + 1];
            child.row = 0;
            child.slotX = x;
            child.slotY = 0;
            child.slotW = childWidths[i];
            child.slotH = ch;
            rowSlots.append(&child);
            x += child.slotW;
        }
        flushRowToRight(rowSlots, cw);
        return makeResult(working, cw, ch);
    }

    int row0H = std::max(1, static_cast<int>(std::round(ch * config.mainRowHeightRatio)));
    row0H = std::min(row0H, ch);
    const int row1H = ch - row0H;
    const int topChildCount = 2;
    const int bottomChildCount = childCount - topChildCount;

    int mainWMulti = mainSlotWidthForArea(cw, ch, row0H, config.mainAreaRatio);
    const int minTopChildStrip = topChildCount;
    mainWMulti = std::min(mainWMulti, cw - minTopChildStrip);

    mainWin.row = 0;
    mainWin.slotX = 0;
    mainWin.slotY = 0;
    mainWin.slotW = mainWMulti;
    mainWin.slotH = row0H;

    QVector<WorkingWindow*> row0Slots;
    row0Slots.append(&mainWin);

    const int topChildTotalW = cw - mainWMulti;
    const QVector<int> topChildWidths = splitWidth(topChildTotalW, topChildCount);
    int x = mainWMulti;
    for (int i = 0; i < topChildCount; ++i) {
        WorkingWindow& child = working[i + 1];
        child.row = 0;
        child.slotX = x;
        child.slotY = 0;
        child.slotW = topChildWidths[i];
        child.slotH = row0H;
        row0Slots.append(&child);
        x += child.slotW;
    }
    flushRowToRight(row0Slots, cw);
    flushRowToBottom(row0Slots, row0H);

    const QVector<int> bottomChildWidths = splitWidth(cw, bottomChildCount);
    const int row1Y = row0H;
    QVector<WorkingWindow*> row1Slots;
    x = 0;
    for (int i = 0; i < bottomChildCount; ++i) {
        WorkingWindow& child = working[i + 1 + topChildCount];
        child.row = 1;
        child.slotX = x;
        child.slotY = row1Y;
        child.slotW = bottomChildWidths[i];
        child.slotH = row1H;
        row1Slots.append(&child);
        x += child.slotW;
    }
    flushRowToRight(row1Slots, cw);
    flushRowToBottom(row1Slots, ch);

    return makeResult(working, cw, ch);
}

} // namespace LayoutSolver
