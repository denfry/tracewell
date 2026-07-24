// src/app/mvvm/MainViewModel.cpp
#include "pch.h"
#include "mvvm/MainViewModel.h"

#include "core/collector.h"
#include "core/collectors/startup_registry.h"

namespace tw::app {

void MainViewModel::Refresh() {
    statusText_.Set(L"Загрузка...");

    dispatcher_.Submit<std::wstring>(
        []() -> std::wstring {
            tw::StartupRegistryCollector collector;
            tw::CancellationToken token;
            tw::CollectorResult result = collector.collect(token);
            std::wstring text = L"Найдено записей автозагрузки: " +
                                 std::to_wstring(result.payload.size());
            if (!result.errors.empty()) {
                text += L" (есть ошибки чтения части ключей)";
            }
            return text;
        },
        [this](std::wstring text) { statusText_.Set(std::move(text)); });
}

}  // namespace tw::app
