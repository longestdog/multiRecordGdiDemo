#pragma once

#include <QVector>

struct LayoutConfig {
    int canvasW = 1920;
    int canvasH = 1080;
    int slotGap = 0;
    float mainWidthRatio = 0.60f;
    float childWidthRatioTotal = 0.40f;
};

struct LayoutWindowIn {
    int id = 0;
    int srcW = 0;
    int srcH = 0;
    int priority = 0;
};

struct LayoutWindowOut : LayoutWindowIn {
    int row = 0;
    int slotX = 0;
    int slotY = 0;
    int slotW = 0;
    int slotH = 0;
    int drawX = 0;
    int drawY = 0;
    int drawW = 0;
    int drawH = 0;
    float scale = 1.f;
};

struct LayoutResult {
    int canvasW = 1920;
    int canvasH = 1080;
    int pageIndex = 0;
    int pageCount = 1;
    int rowCount = 1;
    bool ok = false;
    QVector<LayoutWindowOut> windows;
};

namespace LayoutSolver {

void containInSlot(int srcW, int srcH, int slotX, int slotY, int slotW, int slotH,
    int& drawX, int& drawY, int& drawW, int& drawH, float& scale);

LayoutResult compute(const QVector<LayoutWindowIn>& windows, const LayoutConfig& config);

} // namespace LayoutSolver
