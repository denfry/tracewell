// src/app/ui/Button.cpp
#include "pch.h"
#include "ui/Button.h"
#include "ui/Theme.h"

using Microsoft::WRL::ComPtr;

namespace tw::app {

void Button::SetHovered(bool hovered) {
    if (hovered_ == hovered) return;
    hovered_ = hovered;
    Invalidate();
}

void Button::Click() {
    if (onClick_) onClick_();
}

void Button::Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
                   const ThemeColors& theme) {
    ComPtr<ID2D1SolidColorBrush> fillBrush;
    context->CreateSolidColorBrush(hovered_ ? theme.accent : theme.surface,
                                    fillBrush.ReleaseAndGetAddressOf());
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    context->CreateSolidColorBrush(theme.border, borderBrush.ReleaseAndGetAddressOf());

    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(Bounds(), 4.0f, 4.0f);
    context->FillRoundedRectangle(roundedRect, fillBrush.Get());
    context->DrawRoundedRectangle(roundedRect, borderBrush.Get());

    ComPtr<IDWriteTextFormat> format;
    dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     14.0f, L"en-us", format.ReleaseAndGetAddressOf());
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ComPtr<ID2D1SolidColorBrush> textBrush;
    context->CreateSolidColorBrush(hovered_ ? theme.surface : theme.text,
                                    textBrush.ReleaseAndGetAddressOf());
    context->DrawText(text_.c_str(), static_cast<UINT32>(text_.size()), format.Get(),
                       Bounds(), textBrush.Get());
}

}  // namespace tw::app
