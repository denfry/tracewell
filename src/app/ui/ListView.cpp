// src/app/ui/ListView.cpp
#include "pch.h"
#include "ui/ListView.h"

#include <algorithm>

#include "ui/Theme.h"

using Microsoft::WRL::ComPtr;

namespace tw::app {

void ListView::SetColumns(std::vector<ListViewColumn> columns) {
    columns_ = std::move(columns);
    Invalidate();
}

void ListView::SetRows(std::vector<std::vector<std::wstring>> rows) {
    rows_ = std::move(rows);
    sortColumn_ = -1;
    Invalidate();
}

int ListView::HitTestHeader(D2D1_POINT_2F point) const {
    if (point.y < bounds_.top || point.y > bounds_.top + kHeaderHeight) {
        return -1;
    }
    float cursor = bounds_.left;
    for (size_t i = 0; i < columns_.size(); ++i) {
        float right = cursor + columns_[i].width;
        if (point.x >= cursor && point.x <= right) {
            return static_cast<int>(i);
        }
        cursor = right;
    }
    return -1;
}

void ListView::SortByColumn(int columnIndex) {
    if (columnIndex < 0 || static_cast<size_t>(columnIndex) >= columns_.size()) {
        return;
    }
    sortAscending_ = (sortColumn_ == columnIndex) ? !sortAscending_ : true;
    sortColumn_ = columnIndex;
    std::sort(rows_.begin(), rows_.end(),
              [this](const std::vector<std::wstring>& a, const std::vector<std::wstring>& b) {
                  return sortAscending_ ? a[sortColumn_] < b[sortColumn_]
                                         : a[sortColumn_] > b[sortColumn_];
              });
    Invalidate();
}

void ListView::Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
                     const ThemeColors& theme) {
    ComPtr<IDWriteTextFormat> headerFormat;
    dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     13.0f, L"en-us", headerFormat.ReleaseAndGetAddressOf());
    ComPtr<IDWriteTextFormat> rowFormat;
    dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     13.0f, L"en-us", rowFormat.ReleaseAndGetAddressOf());
    ComPtr<ID2D1SolidColorBrush> textBrush;
    context->CreateSolidColorBrush(theme.text, textBrush.ReleaseAndGetAddressOf());
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    context->CreateSolidColorBrush(theme.border, borderBrush.ReleaseAndGetAddressOf());

    float cursorX = bounds_.left;
    for (const auto& column : columns_) {
        D2D1_RECT_F headerCell{cursorX, bounds_.top, cursorX + column.width,
                                bounds_.top + kHeaderHeight};
        context->DrawText(column.title.c_str(), static_cast<UINT32>(column.title.size()),
                           headerFormat.Get(), headerCell, textBrush.Get());
        cursorX += column.width;
    }
    context->DrawLine(D2D1::Point2F(bounds_.left, bounds_.top + kHeaderHeight),
                       D2D1::Point2F(cursorX, bounds_.top + kHeaderHeight), borderBrush.Get());

    float rowY = bounds_.top + kHeaderHeight;
    for (const auto& row : rows_) {
        float cellX = bounds_.left;
        for (size_t i = 0; i < columns_.size() && i < row.size(); ++i) {
            D2D1_RECT_F cell{cellX, rowY, cellX + columns_[i].width, rowY + kRowHeight};
            context->DrawText(row[i].c_str(), static_cast<UINT32>(row[i].size()),
                               rowFormat.Get(), cell, textBrush.Get());
            cellX += columns_[i].width;
        }
        rowY += kRowHeight;
    }
}

}  // namespace tw::app
