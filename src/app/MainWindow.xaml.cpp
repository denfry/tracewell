#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::TracewellApp::implementation
{
    void MainWindow::RefreshButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        StatusTextBlock().Text(L"clicked (wiring core call in Task 2)");
    }
}
