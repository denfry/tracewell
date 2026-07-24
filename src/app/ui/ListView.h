// src/app/ui/ListView.h
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "pch.h"
#include "ui/Widget.h"

namespace tw::app {

struct ListViewColumn {
    std::wstring title;
    float width;
};

// Список строк с заголовком; клик по заголовку колонки сортирует строки.
// Сортировка — по текстовому значению соответствующей ячейки, без учёта типа.
class ListView : public Widget {
public:
    void SetColumns(std::vector<ListViewColumn> columns);
    void SetRows(std::vector<std::vector<std::wstring>> rows);

    // Возвращает индекс колонки под точкой в пределах строки заголовка, иначе -1.
    int HitTestHeader(D2D1_POINT_2F point) const;
    void SortByColumn(int columnIndex);

    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
              const ThemeColors& theme) override;

private:
    static constexpr float kHeaderHeight = 28.0f;
    static constexpr float kRowHeight = 24.0f;

    std::vector<ListViewColumn> columns_;
    std::vector<std::vector<std::wstring>> rows_;
    int sortColumn_ = -1;
    bool sortAscending_ = true;
};

}  // namespace tw::app
