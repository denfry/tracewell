#pragma once
#include "App.xaml.g.h"

namespace winrt::TracewellApp::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& e);

    private:
        winrt::Microsoft::UI::Xaml::Window window{ nullptr };
    };
}

namespace winrt::TracewellApp::factory_implementation
{
    struct App : AppT<App, implementation::App>
    {
    };
}
