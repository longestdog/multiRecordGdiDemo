#pragma once

#include "LayoutSolver.h"

#include <QObject>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QVector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

struct DemoChildPreset {
    static constexpr int kMaxChildren = 6;
    static constexpr float kMainMinScale = 0.80f;

    static QString label(int childId);
    static QSize defaultSize(int childId);
    static float minScale(int childId);
    static int priority(int childId);
};

struct DemoWindowSlot {
    int windowId = 0;
    HWND hwnd = nullptr;
    int row = 0;
    int slotX = 0;
    int slotY = 0;
    int slotWidth = 0;
    int slotHeight = 0;
};

// 固定画布 1920×1080；LayoutSolver 自动排版，采集时等比 contain（不裁剪）
class WindowLayoutManager : public QObject
{
    Q_OBJECT
public:
    static constexpr int kCanvasWidth = 1920;
    static constexpr int kCanvasHeight = 1080;
    static constexpr int kChildSpacing = 0;
    static constexpr int kDesktopChainSpacing = 8;

    explicit WindowLayoutManager(QObject* parent = nullptr);

    void setMainWindow(HWND hwnd);
    void setPendingChild(int childId, HWND hwnd);
    void clearPendingChildren();
    void commitPendingLayout();

    QVector<DemoWindowSlot> windowSlots() const;
    QSize canvasSize() const;
    QString layoutKey() const;
    int rowCount() const;
    int pageCount() const;
    QString layoutSummary() const;

    bool hasPendingChanges() const;

signals:
    void pendingChanged();
    void committed();

private:
    static QSize windowSize(HWND hwnd);
    QString layoutKeyFor(HWND main, const QVector<HWND>& children) const;
    LayoutResult computeLayout(HWND main, const QVector<HWND>& children) const;
    void rebuildCommittedSlots(HWND main, const QVector<HWND>& children);

    mutable QMutex mutex_;

    HWND mainHwnd_ = nullptr;
    QVector<HWND> pendingChildren_;
    QVector<HWND> committedChildren_;
    QVector<DemoWindowSlot> committedSlots_;
    QString committedLayoutKey_;
    LayoutResult committedLayout_;
};
