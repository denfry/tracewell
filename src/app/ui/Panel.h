// src/app/ui/Panel.h
#pragma once

#include <memory>
#include <vector>

#include "pch.h"
#include "ui/Widget.h"

namespace tw::app {

enum class PanelOrientation { Vertical, Horizontal };

// Простой stack-layout: располагает детей друг за другом с фиксированным отступом.
class Panel : public Widget {
public:
    explicit Panel(PanelOrientation orientation, float spacing = 8.0f)
        : orientation_(orientation), spacing_(spacing) {}

    void AddChild(std::shared_ptr<Widget> child, float size);
    void Layout();
    Widget* HitTestChildren(D2D1_POINT_2F point) const;

    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
              const ThemeColors& theme) override;

private:
    struct Entry {
        std::shared_ptr<Widget> widget;
        float size;
    };

    PanelOrientation orientation_;
    float spacing_;
    std::vector<Entry> children_;
};

}  // namespace tw::app
