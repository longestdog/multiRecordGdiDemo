#include "LayoutSolver.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kInfeasible = -1;

struct WorkingWindow : LayoutWindowIn {
    int row = 0;
    int slotX = 0;
    int slotY = 0;
    int slotW = 0;
    int slotH = 0;
    int minSlotW = 0;
    float scale = 1.f;
};

int distributeWeight(int priority, int maxPriority)
{
    return std::max(1, maxPriority - priority + 1);
}

QVector<WorkingWindow> toWorking(const QVector<LayoutWindowIn>& windows)
{
    QVector<WorkingWindow> out;
    out.reserve(windows.size());
    for (const LayoutWindowIn& in : windows) {
        WorkingWindow w;
        static_cast<LayoutWindowIn&>(w) = in;
        out.append(w);
    }
    std::sort(out.begin(), out.end(), [](const WorkingWindow& a, const WorkingWindow& b) {
        return a.priority < b.priority;
    });
    return out;
}

bool okScale(float scale, float minScale)
{
    return scale + 1e-4f >= minScale;
}

void distributeRowWidths(QVector<WorkingWindow*>& rowWins, int rowW, int gap)
{
    if (rowWins.isEmpty())
        return;

    int minSum = 0;
    int maxPriority = 0;
    for (WorkingWindow* w : rowWins) {
        minSum += w->minSlotW;
        maxPriority = std::max(maxPriority, w->priority);
    }
    minSum += gap * static_cast<int>(rowWins.size() - 1);

    const int extra = std::max(0, rowW - minSum);
    int weightSum = 0;
    for (WorkingWindow* w : rowWins)
        weightSum += distributeWeight(w->priority, maxPriority);

    for (WorkingWindow* w : rowWins) {
        const int weight = distributeWeight(w->priority, maxPriority);
        w->slotW = w->minSlotW + (weightSum > 0 ? (extra * weight / weightSum) : 0);
        w->slotH = rowWins[0]->slotH;
    }

    int used = 0;
    for (WorkingWindow* w : rowWins)
        used += w->slotW;
    used += gap * static_cast<int>(rowWins.size() - 1);
    if (!rowWins.isEmpty() && used < rowW)
        rowWins.front()->slotW += (rowW - used);
}

bool trySingleRow(QVector<WorkingWindow>& windows, int cw, int ch, int gap)
{
    const int rowH = ch;

    for (WorkingWindow& win : windows) {
        const int mw = LayoutSolver::minSlotWidth(win.srcW, win.srcH, win.minScale, rowH);
        if (mw == kInfeasible)
            return false;
        win.minSlotW = mw;
    }

    int totalMin = 0;
    for (const WorkingWindow& win : windows)
        totalMin += win.minSlotW;
    totalMin += gap * std::max(0, windows.size() - 1);
    if (totalMin > cw)
        return false;

    QVector<WorkingWindow*> ptrs;
    for (WorkingWindow& win : windows) {
        win.row = 0;
        win.slotH = rowH;
        ptrs.append(&win);
    }
    distributeRowWidths(ptrs, cw, gap);

    int x = 0;
    for (WorkingWindow& win : windows) {
        win.slotX = x;
        win.slotY = 0;
        x += win.slotW + gap;
    }
    return true;
}

int estimateChildRows(const QVector<WorkingWindow>& children, int cw, int ch, int gap, float mainRowRatio)
{
    if (children.isEmpty())
        return 1;

    const int row0H = std::max(static_cast<int>(ch * mainRowRatio), 1);
    const int remainH = std::max(1, ch - row0H - gap);
    int rows = 1;
    int x = 0;
    int rowH = remainH;
    for (const WorkingWindow& child : children) {
        const int needW = LayoutSolver::minSlotWidth(child.srcW, child.srcH, child.minScale, rowH);
        if (needW == kInfeasible)
            continue;
        if (x > 0 && x + needW > cw) {
            ++rows;
            x = 0;
        }
        x += needW + gap;
    }
    return 1 + rows;
}

QVector<int> splitRowHeights(int remainH, int rowCount, int gap)
{
    QVector<int> heights;
    if (rowCount <= 0)
        return heights;
    const int totalGap = gap * std::max(0, rowCount - 1);
    const int each = std::max(1, (remainH - totalGap) / rowCount);
    for (int i = 0; i < rowCount; ++i)
        heights.append(each);
    return heights;
}

bool tryMultiRow(QVector<WorkingWindow>& windows, int cw, int ch, int gap, float mainRowRatio, int maxRows)
{
    if (windows.isEmpty())
        return false;

    WorkingWindow& mainWin = windows.front();
    if (mainWin.priority != 0)
        return false;

    QVector<WorkingWindow> children;
    for (int i = 1; i < windows.size(); ++i)
        children.append(windows[i]);

    int row0H = std::max(static_cast<int>(std::round(ch * mainRowRatio)),
        static_cast<int>(std::ceil(mainWin.srcH * mainWin.minScale)));
    row0H = std::min(row0H, ch);

    if (LayoutSolver::minSlotWidth(mainWin.srcW, mainWin.srcH, mainWin.minScale, row0H) == kInfeasible)
        return false;

    mainWin.row = 0;
    mainWin.slotX = 0;
    mainWin.slotY = 0;
    mainWin.slotW = cw;
    mainWin.slotH = row0H;
    mainWin.minSlotW = LayoutSolver::minSlotWidth(mainWin.srcW, mainWin.srcH, mainWin.minScale, row0H);

    if (children.isEmpty())
        return true;

    const int remainH = std::max(1, ch - row0H - gap);
    const int childRowCount = std::min(
        maxRows - 1,
        std::max(1, estimateChildRows(children, cw, ch, gap, mainRowRatio) - 1));
    const QVector<int> rowHeights = splitRowHeights(remainH, childRowCount, gap);

    int currentRow = 1;
    int currentX = 0;
    int rowIndex = 0;
    int rowH = rowHeights.value(0, remainH);
    const int yOffset = row0H + gap;

    QVector<QVector<WorkingWindow*>> rowGroups;
    rowGroups.resize(childRowCount);

    for (WorkingWindow& child : children) {
        int needW = LayoutSolver::minSlotWidth(child.srcW, child.srcH, child.minScale, rowH);
        if (needW == kInfeasible) {
            if (rowIndex + 1 < rowHeights.size()) {
                ++rowIndex;
                rowH = rowHeights[rowIndex];
                ++currentRow;
                currentX = 0;
                needW = LayoutSolver::minSlotWidth(child.srcW, child.srcH, child.minScale, rowH);
            }
            if (needW == kInfeasible)
                return false;
        }

        if (currentX > 0 && currentX + needW > cw) {
            if (rowIndex + 1 >= rowHeights.size())
                return false;
            ++rowIndex;
            rowH = rowHeights[rowIndex];
            ++currentRow;
            currentX = 0;
            needW = LayoutSolver::minSlotWidth(child.srcW, child.srcH, child.minScale, rowH);
            if (needW == kInfeasible)
                return false;
        }

        child.row = currentRow;
        child.minSlotW = needW;
        child.slotH = rowH;
        const int groupIndex = currentRow - 1;
        if (groupIndex >= 0 && groupIndex < rowGroups.size())
            rowGroups[groupIndex].append(&child);
        currentX += needW + gap;
    }

    int y = yOffset;
    for (int ri = 0; ri < rowGroups.size(); ++ri) {
        auto& group = rowGroups[ri];
        if (group.isEmpty())
            continue;
        distributeRowWidths(group, cw, gap);
        int x = 0;
        for (WorkingWindow* w : group) {
            w->slotX = x;
            w->slotY = y;
            x += w->slotW + gap;
        }
        y += group[0]->slotH + gap;
    }

    return true;
}

bool refineAndValidate(QVector<WorkingWindow>& windows)
{
    for (WorkingWindow& win : windows) {
        int drawX = 0;
        int drawY = 0;
        int drawW = 0;
        int drawH = 0;
        LayoutSolver::containInSlot(
            win.srcW, win.srcH, win.slotX, win.slotY, win.slotW, win.slotH,
            drawX, drawY, drawW, drawH, win.scale);
        if (!okScale(win.scale, win.minScale))
            return false;
    }
    return true;
}

LayoutResult makeResult(const QVector<WorkingWindow>& windows, int cw, int ch, bool ok)
{
    LayoutResult result;
    result.canvasW = cw;
    result.canvasH = ch;
    result.ok = ok;
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

bool tryLayoutPage(QVector<LayoutWindowIn>& pageWindows, const LayoutConfig& config, int cw, int ch, LayoutResult& out)
{
    auto working = toWorking(pageWindows);
    if (working.isEmpty())
        return false;

    bool laidOut = trySingleRow(working, cw, ch, config.slotGap);
    if (!laidOut)
        laidOut = tryMultiRow(working, cw, ch, config.slotGap, config.mainRowHeightRatio, config.maxRows);
    if (!laidOut)
        return false;
    if (!refineAndValidate(working))
        return false;

    out = makeResult(working, cw, ch, true);
    return true;
}

QVector<QVector<LayoutWindowIn>> splitPages(const QVector<LayoutWindowIn>& windows, const LayoutConfig& config)
{
    QVector<LayoutWindowIn> sorted = windows;
    std::sort(sorted.begin(), sorted.end(), [](const LayoutWindowIn& a, const LayoutWindowIn& b) {
        return a.priority < b.priority;
    });

    LayoutWindowIn mainWin;
    QVector<LayoutWindowIn> children;
    for (const LayoutWindowIn& w : sorted) {
        if (w.priority == 0)
            mainWin = w;
        else
            children.append(w);
    }

    QVector<QVector<LayoutWindowIn>> pages;
    if (!config.paginateWhenChildGt5 || children.size() <= config.maxChildrenPerPage) {
        QVector<LayoutWindowIn> page;
        page.append(mainWin);
        page += children;
        pages.append(page);
        return pages;
    }

    for (int i = 0; i < children.size(); i += config.maxChildrenPerPage) {
        QVector<LayoutWindowIn> page;
        page.append(mainWin);
        for (int j = i; j < i + config.maxChildrenPerPage && j < children.size(); ++j)
            page.append(children[j]);
        pages.append(page);
    }
    return pages;
}

} // namespace

namespace LayoutSolver {

int minSlotWidth(int srcW, int srcH, float minScale, int rowH)
{
    if (srcW <= 0 || srcH <= 0 || rowH <= 0 || minScale <= 0.f)
        return kInfeasible;
    if (rowH < static_cast<int>(std::ceil(srcH * minScale)))
        return kInfeasible;
    return static_cast<int>(std::ceil(srcW * minScale));
}

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

    const QVector<QVector<LayoutWindowIn>> pages = splitPages(windows, config);
    QVector<LayoutWindowIn> page0 = pages.value(0);
    if (page0.isEmpty())
        return empty;

    auto attempt = [&](int cw, int ch, LayoutResult& out) -> bool {
        QVector<LayoutWindowIn> current = page0;
        while (true) {
            if (tryLayoutPage(current, config, cw, ch, out))
                return true;
            if (current.size() <= 1)
                break;
            current.removeLast();
        }
        return false;
    };

    LayoutResult result;
    int cw = config.canvasW;
    int ch = config.canvasH;

    if (attempt(cw, ch, result)) {
        result.pageIndex = 0;
        result.pageCount = pages.size();
        return result;
    }

    if (config.dynamicUpscale) {
        while (cw < config.maxUpscaleW && ch < config.maxUpscaleH) {
            cw = std::min(static_cast<int>(cw * 1.25), config.maxUpscaleW);
            ch = std::min(static_cast<int>(ch * 1.25), config.maxUpscaleH);
            if (attempt(cw, ch, result)) {
                result.pageIndex = 0;
                result.pageCount = pages.size();
                return result;
            }
        }
    }

    QVector<LayoutWindowIn> degraded = page0;
    while (degraded.size() > 1) {
        degraded.removeLast();
        for (LayoutWindowIn& w : degraded) {
            if (w.priority > 0)
                w.minScale = std::max(config.minScaleHardFloor, w.minScale * 0.95f);
        }
        page0 = degraded;
        if (attempt(config.canvasW, config.canvasH, result)) {
            result.pageIndex = 0;
            result.pageCount = pages.size();
            return result;
        }
    }

    return empty;
}

} // namespace LayoutSolver
