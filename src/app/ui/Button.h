// src/app/ui/Button.h
#pragma once

#include <functional>
#include <string>

#include "pch.h"
#include "ui/Widget.h"

namespace tw::app {

class Button : public Widget {
public:
    void SetText(std::wstring text) { text_ = std::move(text); Invalidate(); }
    void SetOnClick(std::function<void()> handler) { onClick_ = std::move(handler); }

    void SetHovered(bool hovered);
    void Click();

    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
              const ThemeColors& theme) override;

private:
    std::wstring text_;
    std::function<void()> onClick_;
    bool hovered_ = false;
};

}  // namespace tw::app
