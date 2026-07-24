// src/app/ui/Widget.h
#pragma once

#include "pch.h"

namespace tw::app {

struct ThemeColors;

// База виджет-дерева: границы, hit-test, отрисовка, dirty-флаг.
class Widget {
public:
    virtual ~Widget() = default;

    void SetBounds(D2D1_RECT_F bounds) { bounds_ = bounds; Invalidate(); }
    const D2D1_RECT_F& Bounds() const { return bounds_; }

    virtual bool HitTest(D2D1_POINT_2F point) const {
        return point.x >= bounds_.left && point.x <= bounds_.right &&
               point.y >= bounds_.top && point.y <= bounds_.bottom;
    }

    virtual void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
                       const ThemeColors& theme) = 0;

    void Invalidate() { dirty_ = true; }
    bool IsDirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

protected:
    D2D1_RECT_F bounds_{0, 0, 0, 0};
    bool dirty_ = true;
};

}  // namespace tw::app
