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

float DemoChildPreset::minScale(int childId)
{
    switch (childId) {
    case 1: return 0.60f;
    case 2: return 0.55f;
    case 3: return 0.55f;
    case 4: return 0.60f;
    case 5: return 0.55f;
    case 6: return 0.55f;
    default: return 0.60f;
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
    mainIn.minScale = DemoChildPreset::kMainMinScale;
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
        childIn.minScale = DemoChildPreset::minScale(childId);
        childIn.priority = DemoChildPreset::priority(childId);
        inputs.append(childIn);
    }

    LayoutConfig config;
    config.canvasW = kCanvasWidth;
    config.canvasH = kCanvasHeight;
    config.slotGap = kChildSpacing;
    config.dynamicUpscale = false;
    config.paginateWhenChildGt5 = true;
    return LayoutSolver::compute(inputs, config);
}

void WindowLayoutManager::rebuildCommittedSlots(HWND main, const QVector<HWND>& children)
{
    committedSlots_.clear();
    committedLayout_ = computeLayout(main, children);
    if (!committedLayout_.ok)
        return;

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
        } else if (out.id >= 1 && out.id <= children.size()) {
            slot.hwnd = children[out.id - 1];
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

    QString summary = QStringLiteral("%1 行，第 1/%2 页")
        .arg(committedLayout_.rowCount)
        .arg(committedLayout_.pageCount);
    if (committedLayout_.pageCount > 1)
        summary += QStringLiteral("（其余子窗在第 2 页及以后）");
    return summary;
}

QVector<DemoWindowSlot> WindowLayoutManager::windowSlots() const
{
    QMutexLocker lock(&mutex_);
    return committedSlots_;
}
