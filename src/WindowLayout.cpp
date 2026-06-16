#include "WindowLayout.h"

#include <QtGlobal>

QString DemoChildPreset::label(int childId)
{
    switch (childId) {
    case 1: return QStringLiteral("跨房连麦");
    case 2: return QStringLiteral("小游戏(大)");
    case 3: return QStringLiteral("小游戏(小)");
    case 4: return QStringLiteral("WebView 4");
    case 5: return QStringLiteral("WebView 5");
    case 6: return QStringLiteral("WebView 6");
    default: return QStringLiteral("WebView %1").arg(childId);
    }
}

QSize DemoChildPreset::defaultSize(int childId)
{
    switch (childId) {
    case 1: return QSize(450, 602);
    case 2: return QSize(488, 769);
    case 3: return QSize(360, 739);
    case 4: return QSize(450, 602);
    case 5: return QSize(488, 769);
    case 6: return QSize(360, 739);
    default: return QSize(480, 640);
    }
}

int DemoChildPreset::priority(int childId)
{
    return childId;
}

WindowLayoutManager::WindowLayoutManager(QObject* parent)
    : QObject(parent)
    , pendingChildren_(DemoChildPreset::kMaxChildren, nullptr)
    , committedChildren_(DemoChildPreset::kMaxChildren, nullptr)
{
}

QSize WindowLayoutManager::windowSize(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return QSize();

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect))
        return QSize();
    return QSize(rect.right - rect.left, rect.bottom - rect.top);
}

void WindowLayoutManager::setMainWindow(HWND hwnd)
{
    QMutexLocker lock(&mutex_);
    if (mainHwnd_ == hwnd)
        return;
    mainHwnd_ = hwnd;
    committedLayoutKey_.clear();
    lock.unlock();
    emit pendingChanged();
    if (hwnd)
        commitPendingLayout();
}

void WindowLayoutManager::setPendingChild(int childId, HWND hwnd)
{
    if (childId < 1 || childId > DemoChildPreset::kMaxChildren)
        return;

    QMutexLocker lock(&mutex_);
    const int index = childId - 1;
    if (pendingChildren_[index] == hwnd)
        return;
    pendingChildren_[index] = hwnd;
    lock.unlock();
    emit pendingChanged();
}

void WindowLayoutManager::clearPendingChildren()
{
    QMutexLocker lock(&mutex_);
    bool changed = false;
    for (HWND hwnd : pendingChildren_) {
        if (hwnd) {
            changed = true;
            break;
        }
    }
    if (!changed)
        return;
    pendingChildren_.fill(nullptr);
    lock.unlock();
    emit pendingChanged();
}

bool WindowLayoutManager::hasPendingChanges() const
{
    QMutexLocker lock(&mutex_);
    return layoutKeyFor(mainHwnd_, committedChildren_) != layoutKeyFor(mainHwnd_, pendingChildren_);
}

LayoutResult WindowLayoutManager::computeLayout(HWND main, const QVector<HWND>& children) const
{
    LayoutResult empty;
    empty.ok = false;
    if (!main || !IsWindow(main))
        return empty;

    QVector<LayoutWindowIn> inputs;
    const QSize mainSize = windowSize(main);
    if (!mainSize.isValid())
        return empty;

    LayoutWindowIn mainIn;
    mainIn.id = 0;
    mainIn.srcW = mainSize.width();
    mainIn.srcH = mainSize.height();
    mainIn.priority = 0;
    inputs.append(mainIn);

    for (int i = 0; i < children.size(); ++i) {
        const HWND hwnd = children[i];
        if (!hwnd || !IsWindow(hwnd))
            continue;

        const int childId = i + 1;
        const QSize size = windowSize(hwnd);
        if (!size.isValid())
            continue;

        LayoutWindowIn childIn;
        childIn.id = childId;
        childIn.srcW = size.width();
        childIn.srcH = size.height();
        childIn.priority = DemoChildPreset::priority(childId);
        inputs.append(childIn);
    }

    LayoutConfig config;
    config.canvasW = kCanvasWidth;
    config.canvasH = kCanvasHeight;
    config.slotGap = kChildSpacing;
    config.mainWidthRatio = DemoChildPreset::kMainWidthRatio;
    config.mainAreaRatio = DemoChildPreset::kMainAreaRatio;
    config.mainRowHeightRatio = DemoChildPreset::kMainRowHeightRatio;
    config.childWidthRatioTotal = DemoChildPreset::kChildWidthRatioTotal;
    return LayoutSolver::compute(inputs, config);
}

void WindowLayoutManager::buildFallbackSlots(HWND main, const QVector<HWND>& children)
{
    committedSlots_.clear();
    committedLayout_ = LayoutResult();
    committedLayout_.canvasW = kCanvasWidth;
    committedLayout_.canvasH = kCanvasHeight;
    committedLayout_.pageCount = 1;
    committedLayout_.pageIndex = 0;

    if (!main || !IsWindow(main)) {
        committedLayout_.ok = false;
        return;
    }

    QVector<HWND> childHwnds;
    for (int i = 0; i < children.size(); ++i) {
        const HWND hwnd = children[i];
        if (hwnd && IsWindow(hwnd))
            childHwnds.append(hwnd);
    }

    const int childCount = childHwnds.size();
    if (childCount <= 0) {
        DemoWindowSlot mainSlot;
        mainSlot.windowId = 0;
        mainSlot.hwnd = main;
        mainSlot.slotWidth = kCanvasWidth;
        mainSlot.slotHeight = kCanvasHeight;
        committedSlots_.append(mainSlot);
        committedLayout_.ok = true;
        committedLayout_.rowCount = 1;
        return;
    }

    const int mainW = (kCanvasWidth * static_cast<int>(DemoChildPreset::kMainWidthRatio * 100)) / 100;

    if (childCount <= 2) {
        const int childTotalW = kCanvasWidth - mainW;
        const QVector<int> childWidths = [&]() {
            QVector<int> widths;
            widths.resize(childCount);
            const int base = childTotalW / childCount;
            const int rem = childTotalW % childCount;
            for (int i = 0; i < childCount; ++i)
                widths[i] = base + (i < rem ? 1 : 0);
            return widths;
        }();

        DemoWindowSlot mainSlot;
        mainSlot.windowId = 0;
        mainSlot.hwnd = main;
        mainSlot.slotX = 0;
        mainSlot.slotY = 0;
        mainSlot.slotWidth = mainW;
        mainSlot.slotHeight = kCanvasHeight;
        committedSlots_.append(mainSlot);

        int x = mainW;
        for (int i = 0; i < childCount; ++i) {
            DemoWindowSlot slot;
            slot.windowId = i + 1;
            slot.hwnd = childHwnds[i];
            slot.slotX = x;
            slot.slotY = 0;
            slot.slotWidth = childWidths[i];
            slot.slotHeight = kCanvasHeight;
            committedSlots_.append(slot);
            x += childWidths[i];
        }

        committedLayout_.ok = true;
        committedLayout_.rowCount = 1;
        return;
    }

    const int row0H = std::max(1, static_cast<int>(kCanvasHeight * DemoChildPreset::kMainRowHeightRatio + 0.5f));
    const int row1H = kCanvasHeight - row0H;
    const int targetMainArea = static_cast<int>(DemoChildPreset::kMainAreaRatio * kCanvasWidth * kCanvasHeight + 0.5f);
    int mainWMulti = std::max(1, std::min(kCanvasWidth - 2, (targetMainArea + row0H - 1) / row0H));
    const int topChildTotalW = kCanvasWidth - mainWMulti;
    const int topChildW = topChildTotalW / 2;
    const int topChildRem = topChildTotalW % 2;

    DemoWindowSlot mainSlot;
    mainSlot.windowId = 0;
    mainSlot.hwnd = main;
    mainSlot.row = 0;
    mainSlot.slotX = 0;
    mainSlot.slotY = 0;
    mainSlot.slotWidth = mainWMulti;
    mainSlot.slotHeight = row0H;
    committedSlots_.append(mainSlot);

    int x = mainWMulti;
    for (int i = 0; i < 2; ++i) {
        const int slotW = topChildW + (i < topChildRem ? 1 : 0);
        DemoWindowSlot slot;
        slot.windowId = i + 1;
        slot.hwnd = childHwnds[i];
        slot.row = 0;
        slot.slotX = x;
        slot.slotY = 0;
        slot.slotWidth = slotW;
        slot.slotHeight = row0H;
        committedSlots_.append(slot);
        x += slotW;
    }

    const int bottomCount = childCount - 2;
    const int bottomBaseW = kCanvasWidth / bottomCount;
    const int bottomRem = kCanvasWidth % bottomCount;
    x = 0;
    for (int i = 0; i < bottomCount; ++i) {
        const int slotW = bottomBaseW + (i < bottomRem ? 1 : 0);
        DemoWindowSlot slot;
        slot.windowId = i + 3;
        slot.hwnd = childHwnds[i + 2];
        slot.row = 1;
        slot.slotX = x;
        slot.slotY = row0H;
        slot.slotWidth = slotW;
        slot.slotHeight = row1H;
        committedSlots_.append(slot);
        x += slotW;
    }

    committedLayout_.ok = true;
    committedLayout_.rowCount = 2;
}

void WindowLayoutManager::rebuildCommittedSlots(HWND main, const QVector<HWND>& children)
{
    committedSlots_.clear();
    committedLayout_ = computeLayout(main, children);
    if (!committedLayout_.ok) {
        buildFallbackSlots(main, children);
        return;
    }

    for (const LayoutWindowOut& out : committedLayout_.windows) {
        DemoWindowSlot slot;
        slot.windowId = out.id;
        slot.row = out.row;
        slot.slotX = out.slotX;
        slot.slotY = out.slotY;
        slot.slotWidth = out.slotW;
        slot.slotHeight = out.slotH;

        if (out.id == 0) {
            slot.hwnd = main;
        } else {
            const int index = out.id - 1;
            if (index >= 0 && index < children.size())
                slot.hwnd = children[index];
        }
        if (!slot.hwnd || !IsWindow(slot.hwnd))
            continue;
        committedSlots_.append(slot);
    }
}

void WindowLayoutManager::commitPendingLayout()
{
    QMutexLocker lock(&mutex_);
    const QString nextKey = layoutKeyFor(mainHwnd_, pendingChildren_);
    if (nextKey == committedLayoutKey_)
        return;

    committedChildren_ = pendingChildren_;
    rebuildCommittedSlots(mainHwnd_, committedChildren_);
    committedLayoutKey_ = nextKey;
    lock.unlock();
    emit committed();
}

QString WindowLayoutManager::layoutKeyFor(HWND main, const QVector<HWND>& children) const
{
    QString key = QStringLiteral("canvas=%1x%2;gap=%3;")
        .arg(kCanvasWidth)
        .arg(kCanvasHeight)
        .arg(kChildSpacing);

    if (main && IsWindow(main)) {
        const QSize size = windowSize(main);
        key += QStringLiteral("main=%1,%2,%3;")
            .arg(reinterpret_cast<quintptr>(main))
            .arg(size.width())
            .arg(size.height());
    }

    for (int i = 0; i < children.size(); ++i) {
        const HWND hwnd = children[i];
        if (!hwnd || !IsWindow(hwnd))
            continue;
        const QSize size = windowSize(hwnd);
        key += QStringLiteral("c%1=%2,%3,%4;")
            .arg(i + 1)
            .arg(reinterpret_cast<quintptr>(hwnd))
            .arg(size.width())
            .arg(size.height());
    }

    const LayoutResult layout = computeLayout(main, children);
    if (layout.ok) {
        key += QStringLiteral("rows=%1;pages=%2;")
            .arg(layout.rowCount)
            .arg(layout.pageCount);
        for (const LayoutWindowOut& out : layout.windows) {
            key += QStringLiteral("s%1=%2,%3,%4,%5,%6;")
                .arg(out.id)
                .arg(out.row)
                .arg(out.slotX)
                .arg(out.slotY)
                .arg(out.slotW)
                .arg(out.slotH);
        }
    }
    return key;
}

QString WindowLayoutManager::layoutKey() const
{
    QMutexLocker lock(&mutex_);
    return committedLayoutKey_;
}

QSize WindowLayoutManager::canvasSize() const
{
    QMutexLocker lock(&mutex_);
    if (!mainHwnd_ || !IsWindow(mainHwnd_) || !committedLayout_.ok)
        return QSize();
    return QSize(committedLayout_.canvasW, committedLayout_.canvasH);
}

int WindowLayoutManager::rowCount() const
{
    QMutexLocker lock(&mutex_);
    return committedLayout_.ok ? committedLayout_.rowCount : 0;
}

int WindowLayoutManager::pageCount() const
{
    QMutexLocker lock(&mutex_);
    return committedLayout_.ok ? committedLayout_.pageCount : 0;
}

QString WindowLayoutManager::layoutSummary() const
{
    QMutexLocker lock(&mutex_);
    if (!committedLayout_.ok)
        return QString();

    const int childSlots = std::max(0, committedSlots_.size() - 1);
    if (childSlots <= 0)
        return QStringLiteral("仅主窗，contain 居中填满画布");

    if (childSlots <= 2) {
        const int childPercent = static_cast<int>(DemoChildPreset::kChildWidthRatioTotal * 100 / childSlots);
        return QStringLiteral("1 行：主窗 60%%，%1 个子窗各 %2%%，槽内 contain 居中")
            .arg(childSlots)
            .arg(childPercent);
    }

    const int bottomCount = childSlots - 2;
    return QStringLiteral("2 行：主窗槽位面积 60%%，前 2 子窗右上并排；第 2 排 %1 个子窗均分整行")
        .arg(bottomCount);
}

QVector<DemoWindowSlot> WindowLayoutManager::windowSlots() const
{
    QMutexLocker lock(&mutex_);
    return committedSlots_;
}

int WindowLayoutManager::pendingWindowCount() const
{
    QMutexLocker lock(&mutex_);
    int count = (mainHwnd_ && IsWindow(mainHwnd_)) ? 1 : 0;
    for (HWND hwnd : pendingChildren_) {
        if (hwnd && IsWindow(hwnd))
            ++count;
    }
    return count;
}
