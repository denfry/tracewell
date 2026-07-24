#pragma once
#include "MainWindow.g.h"

namespace winrt::TracewellApp::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow() { InitializeComponent(); }

        void RefreshButton_Click(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& e);
    };
}

namespace winrt::TracewellApp::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
