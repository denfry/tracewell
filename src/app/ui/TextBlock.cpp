// src/app/ui/TextBlock.cpp
#include "pch.h"
#include "ui/TextBlock.h"
#include "ui/Theme.h"

using Microsoft::WRL::ComPtr;

namespace tw::app {

void TextBlock::SetText(std::wstring text) {
    text_ = std::move(text);
    Invalidate();
}

void TextBlock::Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
                      const ThemeColors& theme) {
    ComPtr<IDWriteTextFormat> format;
    dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     16.0f, L"en-us", format.ReleaseAndGetAddressOf());
    ComPtr<ID2D1SolidColorBrush> brush;
    context->CreateSolidColorBrush(theme.text, brush.ReleaseAndGetAddressOf());
    context->DrawText(text_.c_str(), static_cast<UINT32>(text_.size()), format.Get(),
                       Bounds(), brush.Get());
}

}  // namespace tw::app
