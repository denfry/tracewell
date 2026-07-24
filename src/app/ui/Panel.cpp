// src/app/ui/Panel.cpp
#include "pch.h"
#include "ui/Panel.h"

namespace tw::app {

void Panel::AddChild(std::shared_ptr<Widget> child, float size) {
    children_.push_back({std::move(child), size});
    Invalidate();
}

void Panel::Layout() {
    float cursor = orientation_ == PanelOrientation::Vertical ? bounds_.top : bounds_.left;
    for (auto& entry : children_) {
        D2D1_RECT_F childBounds = bounds_;
        if (orientation_ == PanelOrientation::Vertical) {
            childBounds.top = cursor;
            childBounds.bottom = cursor + entry.size;
            cursor = childBounds.bottom + spacing_;
        } else {
            childBounds.left = cursor;
            childBounds.right = cursor + entry.size;
            cursor = childBounds.right + spacing_;
        }
        entry.widget->SetBounds(childBounds);
    }
    Invalidate();
}

Widget* Panel::HitTestChildren(D2D1_POINT_2F point) const {
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (it->widget->HitTest(point)) {
            return it->widget.get();
        }
    }
    return nullptr;
}

void Panel::Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
                  const ThemeColors& theme) {
    for (auto& entry : children_) {
        entry.widget->Draw(context, dwriteFactory, theme);
        entry.widget->ClearDirty();
    }
    ClearDirty();
}

}  // namespace tw::app
