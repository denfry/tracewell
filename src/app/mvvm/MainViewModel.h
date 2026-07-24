// src/app/mvvm/MainViewModel.h
#pragma once

#include <string>

#include "dispatch/Dispatcher.h"
#include "mvvm/Property.h"
#include "mvvm/ViewModel.h"

namespace tw::app {

class MainViewModel : public ViewModel {
public:
    explicit MainViewModel(Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

    Property<std::wstring>& StatusText() { return statusText_; }

    // Запускает StartupRegistryCollector в пуле dispatcher_; StatusText
    // обновляется только после доставки результата на UI-поток.
    void Refresh();

private:
    Dispatcher& dispatcher_;
    Property<std::wstring> statusText_{L"Нажмите Refresh"};
};

}  // namespace tw::app
