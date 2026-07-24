// src/app/ui/TextBlock.h
#pragma once

#include <string>

#include "pch.h"
#include "ui/Widget.h"

namespace tw::app {

class TextBlock : public Widget {
public:
    void SetText(std::wstring text);

    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
              const ThemeColors& theme) override;

private:
    std::wstring text_;
};

}  // namespace tw::app
