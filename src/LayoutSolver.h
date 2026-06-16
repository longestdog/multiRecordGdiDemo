#pragma once

#include <QVector>

struct LayoutConfig {
    int canvasW = 1920;
    int canvasH = 1080;
    int slotGap = 0;
    bool dynamicUpscale = false;
    bool paginateWhenChildGt5 = true;
    int maxRows = 3;
    float mainRowHeightRatio = 0.65f;
    int maxUpscaleW = 3840;
    int maxUpscaleH = 2160;
    float minScaleHardFloor = 0.5f;
    int maxChildrenPerPage = 5;
};

struct LayoutWindowIn {
    int id = 0;
    int srcW = 0;
    int srcH = 0;
    float minScale = 0.6f;
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

int minSlotWidth(int srcW, int srcH, float minScale, int rowH);
void containInSlot(int srcW, int srcH, int slotX, int slotY, int slotW, int slotH,
    int& drawX, int& drawY, int& drawW, int& drawH, float& scale);

LayoutResult compute(const QVector<LayoutWindowIn>& windows, const LayoutConfig& config);

} // namespace LayoutSolver
