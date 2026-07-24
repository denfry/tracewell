# Win32 + Direct2D UI Framework Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the abandoned WinUI 3 scaffold in `src/app` with a working Win32 + Direct2D application: window/message loop, a small retained-mode widget toolkit, an MVVM layer without XAML binding, and a thread-pool dispatcher — validated end-to-end with a real Startup screen wired to `tw::StartupRegistryCollector`.

**Architecture:** One MSBuild project (`src/app/App.vcxproj`), plain Win32 desktop app (no Windows App SDK, no NuGet `PackageReference` — this sidesteps the NuGet TFM restore bug recorded in ADR 0001 entirely, since a plain desktop project never invokes NuGet restore). DXGI swap chain + `ID2D1DeviceContext` rendering, DirectWrite text, a thread-pool `Dispatcher` marshalling background collector calls back to the UI thread via a custom `WM_APP_DISPATCH` message, and explicit-subscription `Property<T>` observables driving widget redraws.

**Tech Stack:** C++20, Win32 API, Direct2D 1.1 (`ID2D1DeviceContext`), Direct3D 11, DXGI 1.2, DirectWrite, Microsoft::WRL::ComPtr (no C++/WinRT). Existing CMake/vcpkg tree (`tracewell-core`, Catch2 tests) untouched except two new test files.

## Global Constraints

- C++20 everywhere (`LanguageStandard=stdcpp20` in the vcxproj matches root `CMakeLists.txt`'s `CMAKE_CXX_STANDARD 20`).
- No Windows App SDK, no NuGet `PackageReference` in `App.vcxproj` — plain Win32 desktop app only (see ADR 0001, `docs/adr/0001-winui3-poc-fallback-to-win32-d2d.md`).
- `CharacterSet=Unicode`, all Win32 calls use the `W` (wide) variant — matches root CMake's `UNICODE _UNICODE` compile definitions.
- Link `tracewell-core.lib` from `build/src/<Config>` (CMake preset `default` output layout — same path pattern the original WinUI3 vcxproj used).
- DPI awareness: existing `src/app/App.manifest` already declares `PerMonitorV2` — do not modify it.
- `Property<T>` and `Dispatcher` (the only components the spec requires unit tests for) must build and run under plain CMake/Catch2 with zero WinAPI/Direct2D dependency, so they can be added to `tests/CMakeLists.txt` and exercised by `ctest` — this is why they get `<PrecompiledHeader>NotUsing</PrecompiledHeader>` in the vcxproj and never `#include "pch.h"`.
- All core/collector calls happen only inside a `Dispatcher::Submit` worker lambda — never on the UI thread. Every task below that touches this boundary must preserve that invariant.

---

### Task 1: `Property<T>` observable

**Files:**
- Create: `src/app/mvvm/Property.h`
- Test: `tests/test_property.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `tw::app::Property<T>` — `explicit Property(T initial = T{})`, `const T& Get() const`, `void Set(T value)`, `int Subscribe(std::function<void(const T&)> callback)`, `void Unsubscribe(int id)`.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_property.cpp
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/mvvm/Property.h"

using tw::app::Property;

TEST_CASE("Property::Set notifies subscribers with new value") {
    Property<int> prop(0);
    int observed = -1;
    prop.Subscribe([&](const int& v) { observed = v; });

    prop.Set(42);

    REQUIRE(observed == 42);
    REQUIRE(prop.Get() == 42);
}

TEST_CASE("Property::Unsubscribe stops further notifications") {
    Property<int> prop(0);
    int callCount = 0;
    int id = prop.Subscribe([&](const int&) { ++callCount; });

    prop.Set(1);
    prop.Unsubscribe(id);
    prop.Set(2);

    REQUIRE(callCount == 1);
}

TEST_CASE("Property supports multiple independent subscribers") {
    Property<std::string> prop("init");
    std::vector<std::string> a, b;
    prop.Subscribe([&](const std::string& v) { a.push_back(v); });
    prop.Subscribe([&](const std::string& v) { b.push_back(v); });

    prop.Set("x");

    REQUIRE(a == std::vector<std::string>{"x"});
    REQUIRE(b == std::vector<std::string>{"x"});
}
```

- [ ] **Step 2: Register the test file in CMake**

Modify `tests/CMakeLists.txt`:

```cmake
find_package(Catch2 3 CONFIG REQUIRED)

add_executable(tracewell-tests
    test_diff.cpp
    test_storage.cpp
    test_property.cpp
)
target_link_libraries(tracewell-tests PRIVATE tracewell-core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(tracewell-tests)
```

- [ ] **Step 3: Run tests to verify they fail (header doesn't exist yet)**

Run: `cmake --build build --config Release --target tracewell-tests`
Expected: FAIL — `app/mvvm/Property.h: No such file or directory`

- [ ] **Step 4: Write the implementation**

```cpp
// src/app/mvvm/Property.h
#pragma once

#include <functional>
#include <mutex>
#include <unordered_map>

namespace tw::app {

// Простая observable-обёртка: явная подписка вместо XAML {Binding}.
// Set() уведомляет подписчиков синхронно, на потоке вызывающего.
template <typename T>
class Property {
public:
    using Callback = std::function<void(const T&)>;

    explicit Property(T initial = T{}) : value_(std::move(initial)) {}

    const T& Get() const { return value_; }

    void Set(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            value_ = std::move(value);
        }
        NotifySubscribers();
    }

    // Возвращает id подписки для последующей отписки через Unsubscribe.
    int Subscribe(Callback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        int id = nextId_++;
        subscribers_.emplace(id, std::move(callback));
        return id;
    }

    void Unsubscribe(int id) {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(id);
    }

private:
    void NotifySubscribers() {
        std::unordered_map<int, Callback> snapshot;
        T valueCopy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = subscribers_;
            valueCopy = value_;
        }
        for (auto& [id, callback] : snapshot) {
            callback(valueCopy);
        }
    }

    mutable std::mutex mutex_;
    T value_;
    std::unordered_map<int, Callback> subscribers_;
    int nextId_ = 0;
};

}  // namespace tw::app
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --config Release --target tracewell-tests && ctest --test-dir build -C Release -R Property --output-on-failure`
Expected: 3/3 PASS

- [ ] **Step 6: Commit**

```bash
git add src/app/mvvm/Property.h tests/test_property.cpp tests/CMakeLists.txt
git commit -m "app: add Property<T> observable for MVVM without XAML binding"
```

---

### Task 2: `Dispatcher` thread pool

**Files:**
- Create: `src/app/dispatch/Dispatcher.h`
- Create: `src/app/dispatch/Dispatcher.cpp`
- Test: `tests/test_dispatcher.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `tw::app::Dispatcher` — `explicit Dispatcher(Notify notify, int workerCount = 2)` where `using Notify = std::function<void()>`; `template <typename T> void Submit(std::function<T()> task, std::function<void(T)> onComplete)`; `void DrainUiQueue()`.
- Consumes: nothing from Task 1.

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_dispatcher.cpp
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include <catch2/catch_test_macros.hpp>

#include "app/dispatch/Dispatcher.h"

using tw::app::Dispatcher;

TEST_CASE("Dispatcher delivers a result only after DrainUiQueue is called") {
    std::mutex m;
    std::condition_variable cv;
    bool notified = false;
    Dispatcher dispatcher(
        [&] {
            std::lock_guard<std::mutex> lock(m);
            notified = true;
            cv.notify_one();
        },
        2);

    int received = -1;
    dispatcher.Submit<int>([] { return 42; }, [&](int v) { received = v; });

    std::unique_lock<std::mutex> lock(m);
    REQUIRE(cv.wait_for(lock, std::chrono::seconds(2), [&] { return notified; }));
    lock.unlock();

    REQUIRE(received == -1);  // ещё не применено — ждёт вычитывания на "UI-потоке"
    dispatcher.DrainUiQueue();
    REQUIRE(received == 42);
}

TEST_CASE("Dispatcher processes many tasks with a bounded worker pool") {
    std::mutex m;
    std::condition_variable cv;
    int notifyCount = 0;
    constexpr int kTasks = 20;
    Dispatcher dispatcher(
        [&] {
            std::lock_guard<std::mutex> lock(m);
            ++notifyCount;
            cv.notify_one();
        },
        2);

    std::atomic<int> sum{0};
    for (int i = 0; i < kTasks; ++i) {
        dispatcher.Submit<int>([i] { return i; }, [&](int v) { sum.fetch_add(v); });
    }

    std::unique_lock<std::mutex> lock(m);
    REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return notifyCount == kTasks; }));
    lock.unlock();

    dispatcher.DrainUiQueue();
    REQUIRE(sum.load() == (kTasks * (kTasks - 1)) / 2);
}
```

- [ ] **Step 2: Register the test file and Dispatcher.cpp in CMake**

Modify `tests/CMakeLists.txt`:

```cmake
find_package(Catch2 3 CONFIG REQUIRED)

add_executable(tracewell-tests
    test_diff.cpp
    test_storage.cpp
    test_property.cpp
    test_dispatcher.cpp
    ../src/app/dispatch/Dispatcher.cpp
)
target_link_libraries(tracewell-tests PRIVATE tracewell-core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(tracewell-tests)
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake --build build --config Release --target tracewell-tests`
Expected: FAIL — `app/dispatch/Dispatcher.h: No such file or directory`

- [ ] **Step 4: Write `Dispatcher.h`**

```cpp
// src/app/dispatch/Dispatcher.h
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace tw::app {

// Пул фоновых воркеров + потокобезопасная доставка результатов на UI-поток.
// notify() вызывается на воркер-потоке после того, как результат положен в
// очередь; продакшен-код передаёт сюда PostMessage(hwnd, WM_APP_DISPATCH, 0, 0).
// onComplete никогда не вызывается иначе как изнутри DrainUiQueue().
class Dispatcher {
public:
    using Notify = std::function<void()>;

    explicit Dispatcher(Notify notify, int workerCount = 2);
    ~Dispatcher();

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    template <typename T>
    void Submit(std::function<T()> task, std::function<void(T)> onComplete) {
        EnqueueJob([this, task = std::move(task), onComplete = std::move(onComplete)]() mutable {
            T result = task();
            PostToUiThread([onComplete = std::move(onComplete), result = std::move(result)]() mutable {
                onComplete(std::move(result));
            });
        });
    }

    // Вызывается на UI-потоке (например, из обработчика WM_APP_DISPATCH).
    void DrainUiQueue();

private:
    void EnqueueJob(std::function<void()> job);
    void PostToUiThread(std::function<void()> completion);
    void WorkerLoop();

    Notify notify_;
    std::vector<std::thread> workers_;

    std::mutex jobMutex_;
    std::condition_variable jobCv_;
    std::queue<std::function<void()>> jobs_;
    bool stopping_ = false;

    std::mutex uiMutex_;
    std::queue<std::function<void()>> uiQueue_;
};

}  // namespace tw::app
```

- [ ] **Step 5: Write `Dispatcher.cpp`**

```cpp
// src/app/dispatch/Dispatcher.cpp
#include "Dispatcher.h"

namespace tw::app {

Dispatcher::Dispatcher(Notify notify, int workerCount) : notify_(std::move(notify)) {
    workers_.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
}

Dispatcher::~Dispatcher() {
    {
        std::lock_guard<std::mutex> lock(jobMutex_);
        stopping_ = true;
    }
    jobCv_.notify_all();
    for (auto& worker : workers_) {
        worker.join();
    }
}

void Dispatcher::EnqueueJob(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(jobMutex_);
        jobs_.push(std::move(job));
    }
    jobCv_.notify_one();
}

void Dispatcher::PostToUiThread(std::function<void()> completion) {
    {
        std::lock_guard<std::mutex> lock(uiMutex_);
        uiQueue_.push(std::move(completion));
    }
    notify_();
}

void Dispatcher::WorkerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(jobMutex_);
            jobCv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        job();
    }
}

void Dispatcher::DrainUiQueue() {
    std::queue<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(uiMutex_);
        std::swap(local, uiQueue_);
    }
    while (!local.empty()) {
        local.front()();
        local.pop();
    }
}

}  // namespace tw::app
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --config Release --target tracewell-tests && ctest --test-dir build -C Release -R Dispatcher --output-on-failure`
Expected: 2/2 PASS

- [ ] **Step 7: Commit**

```bash
git add src/app/dispatch/Dispatcher.h src/app/dispatch/Dispatcher.cpp tests/test_dispatcher.cpp tests/CMakeLists.txt
git commit -m "app: add Dispatcher thread pool for background-to-UI-thread marshalling"
```

---

### Task 3: App project skeleton (replace WinUI3 scaffold with a blank Win32 window)

**Files:**
- Delete: `src/app/App.xaml`, `src/app/App.xaml.cpp`, `src/app/App.xaml.h`, `src/app/MainWindow.xaml`, `src/app/MainWindow.xaml.cpp`, `src/app/MainWindow.xaml.h`, `src/app/pch.cpp`, `src/app/pch.h`
- Create: `src/app/pch.h`, `src/app/pch.cpp`
- Create: `src/app/main.cpp`
- Create: `src/app/shell/MainWindow.h`
- Create: `src/app/shell/MainWindow.cpp`
- Modify: `src/app/App.vcxproj` (full rewrite)
- `src/app/App.manifest` — unchanged, already has `PerMonitorV2`.

**Interfaces:**
- Produces: `tw::app::MainWindow` — `bool Create(HINSTANCE instance, int cmdShow)`, `int RunMessageLoop()`. Later tasks extend this class in place.

- [ ] **Step 1: Delete the WinUI3 scaffold files**

```bash
git rm src/app/App.xaml src/app/App.xaml.cpp src/app/App.xaml.h \
       src/app/MainWindow.xaml src/app/MainWindow.xaml.cpp src/app/MainWindow.xaml.h \
       src/app/pch.cpp src/app/pch.h
```

- [ ] **Step 2: Write the new precompiled header**

```cpp
// src/app/pch.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>
```

```cpp
// src/app/pch.cpp
#include "pch.h"
```

- [ ] **Step 3: Write `main.cpp`**

```cpp
// src/app/main.cpp
#include "pch.h"

#include "shell/MainWindow.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    tw::app::MainWindow window;
    if (!window.Create(hInstance, nCmdShow)) {
        return -1;
    }
    return window.RunMessageLoop();
}
```

- [ ] **Step 4: Write `shell/MainWindow.h`**

```cpp
// src/app/shell/MainWindow.h
#pragma once

#include "pch.h"

namespace tw::app {

class MainWindow {
public:
    bool Create(HINSTANCE instance, int cmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
};

}  // namespace tw::app
```

- [ ] **Step 5: Write `shell/MainWindow.cpp`**

```cpp
// src/app/shell/MainWindow.cpp
#include "pch.h"
#include "shell/MainWindow.h"

namespace tw::app {

namespace {
constexpr wchar_t kClassName[] = L"TracewellMainWindow";
}

bool MainWindow::Create(HINSTANCE instance, int cmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    hwnd_ = CreateWindowExW(0, kClassName, L"Tracewell", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720,
                             nullptr, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }

    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace tw::app
```

- [ ] **Step 6: Rewrite `App.vcxproj`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>

  <PropertyGroup Label="Globals">
    <ProjectGuid>{A1E97CF4-8C93-4B7B-9F1E-6C6B2C9E2D10}</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
    <RootNamespace>TracewellApp</RootNamespace>
    <MinimumVisualStudioVersion>17.0</MinimumVisualStudioVersion>
    <WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>
  </PropertyGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />

  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Label="Configuration" Condition="'$(Configuration)'=='Debug'">
    <UseDebugLibraries>true</UseDebugLibraries>
  </PropertyGroup>
  <PropertyGroup Label="Configuration" Condition="'$(Configuration)'=='Release'">
    <UseDebugLibraries>false</UseDebugLibraries>
    <WholeProgramOptimization>true</WholeProgramOptimization>
  </PropertyGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />

  <ItemGroup>
    <ClInclude Include="pch.h" />
    <ClInclude Include="shell\MainWindow.h" />
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="pch.cpp">
      <PrecompiledHeader>Create</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="main.cpp" />
    <ClCompile Include="shell\MainWindow.cpp" />
  </ItemGroup>
  <ItemGroup>
    <Manifest Include="App.manifest" />
  </ItemGroup>

  <ItemDefinitionGroup>
    <ClCompile>
      <PrecompiledHeader>Use</PrecompiledHeader>
      <PrecompiledHeaderFile>pch.h</PrecompiledHeaderFile>
      <AdditionalIncludeDirectories>$(ProjectDir)..;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <LanguageStandard>stdcpp20</LanguageStandard>
    </ClCompile>
  </ItemDefinitionGroup>

  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <Link>
      <AdditionalLibraryDirectories>$(ProjectDir)..\..\build\src\Debug;$(ProjectDir)..\..\build\vcpkg_installed\x64-windows\debug\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>tracewell-core.lib;sqlite3.lib;pdh.lib;advapi32.lib;shell32.lib;ole32.lib;d3d11.lib;d2d1.lib;dwrite.lib;dxgi.lib;user32.lib;gdi32.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
    <ClCompile>
      <AdditionalIncludeDirectories>$(ProjectDir)..\..\build\vcpkg_installed\x64-windows\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <Link>
      <AdditionalLibraryDirectories>$(ProjectDir)..\..\build\src\Release;$(ProjectDir)..\..\build\vcpkg_installed\x64-windows\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>tracewell-core.lib;sqlite3.lib;pdh.lib;advapi32.lib;shell32.lib;ole32.lib;d3d11.lib;d2d1.lib;dwrite.lib;dxgi.lib;user32.lib;gdi32.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
    <ClCompile>
      <AdditionalIncludeDirectories>$(ProjectDir)..\..\build\vcpkg_installed\x64-windows\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>

  <Target Name="CopySqlite3Dll" AfterTargets="Build">
    <ItemGroup>
      <Sqlite3Dll Condition="'$(Configuration)'=='Release'" Include="$(ProjectDir)..\..\build\vcpkg_installed\x64-windows\bin\sqlite3.dll" />
      <Sqlite3Dll Condition="'$(Configuration)'=='Debug'" Include="$(ProjectDir)..\..\build\vcpkg_installed\x64-windows\debug\bin\sqlite3.dll" />
    </ItemGroup>
    <Copy SourceFiles="@(Sqlite3Dll)" DestinationFolder="$(OutDir)" SkipUnchangedFiles="true" />
  </Target>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
```

- [ ] **Step 7: Build and verify**

Run (PowerShell):
```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild src\app\App.vcxproj /p:Configuration=Release /p:Platform=x64
```
Expected: `Build succeeded. 0 Error(s)`, no NuGet restore step runs at all.

- [ ] **Step 8: Manual smoke check**

Run `src\app\x64\Release\App.exe`. Expected: a blank white window titled "Tracewell" opens and closes cleanly via the X button.

- [ ] **Step 9: Commit**

```bash
git add -A src/app
git commit -m "app: replace WinUI3 scaffold with a blank Win32 window skeleton"
```

---

### Task 4: `RenderDevice` — DXGI + Direct2D device context

**Files:**
- Create: `src/app/render/RenderDevice.h`
- Create: `src/app/render/RenderDevice.cpp`
- Modify: `src/app/shell/MainWindow.h`, `src/app/shell/MainWindow.cpp`
- Modify: `src/app/App.vcxproj` (add new files)

**Interfaces:**
- Produces: `tw::app::RenderDevice` — `bool Initialize(HWND hwnd)`, `void Resize(UINT width, UINT height)`, `void SetDpi(float dpiX, float dpiY)`, `ID2D1DeviceContext* BeginDraw()`, `bool EndDraw()`, `IDWriteFactory* DWriteFactory() const`.
- Consumes: nothing from earlier tasks.

- [ ] **Step 1: Write `render/RenderDevice.h`**

```cpp
// src/app/render/RenderDevice.h
#pragma once

#include "pch.h"

namespace tw::app {

// Владеет D3D11-устройством, DXGI swap chain'ом и D2D1-device context'ом,
// привязанными к одному HWND. Пересоздаётся целиком при потере устройства
// (DXGI_ERROR_DEVICE_REMOVED/RESET, D2DERR_RECREATE_TARGET).
class RenderDevice {
public:
    bool Initialize(HWND hwnd);
    void Resize(UINT width, UINT height);
    void SetDpi(float dpiX, float dpiY);

    // BeginDraw возвращает nullptr, если устройство ещё не готово.
    // EndDraw возвращает false при потере устройства — контекст уже
    // пересоздан внутри, вызывающий код должен запросить перерисовку.
    ID2D1DeviceContext* BeginDraw();
    bool EndDraw();

    IDWriteFactory* DWriteFactory() const { return dwriteFactory_.Get(); }

private:
    bool CreateDeviceResources(HWND hwnd);
    void ReleaseDeviceResources();

    HWND hwnd_ = nullptr;
    float dpiX_ = 96.0f;
    float dpiY_ = 96.0f;

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
};

}  // namespace tw::app
```

- [ ] **Step 2: Write `render/RenderDevice.cpp`**

```cpp
// src/app/render/RenderDevice.cpp
#include "pch.h"
#include "render/RenderDevice.h"

using Microsoft::WRL::ComPtr;

namespace tw::app {

bool RenderDevice::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    return CreateDeviceResources(hwnd);
}

bool RenderDevice::CreateDeviceResources(HWND hwnd) {
    ReleaseDeviceResources();

    UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
        nullptr, 0, D3D11_SDK_VERSION,
        d3dDevice_.ReleaseAndGetAddressOf(), &featureLevel,
        d3dContext_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3dDevice_.As(&dxgiDevice))) {
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf()))) {
        return false;
    }
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(dxgiFactory.ReleaseAndGetAddressOf())))) {
        return false;
    }

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = static_cast<UINT>(clientRect.right - clientRect.left);
    swapChainDesc.Height = static_cast<UINT>(clientRect.bottom - clientRect.top);
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    if (FAILED(dxgiFactory->CreateSwapChainForHwnd(
            d3dDevice_.Get(), hwnd, &swapChainDesc, nullptr, nullptr,
            swapChain_.ReleaseAndGetAddressOf()))) {
        return false;
    }

    D2D1_FACTORY_OPTIONS factoryOptions{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factoryOptions,
                                  d2dFactory_.ReleaseAndGetAddressOf()))) {
        return false;
    }
    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), d2dDevice_.ReleaseAndGetAddressOf()))) {
        return false;
    }
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                d2dContext_.ReleaseAndGetAddressOf()))) {
        return false;
    }

    ComPtr<IDXGISurface> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf())))) {
        return false;
    }
    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpiX_, dpiY_);
    ComPtr<ID2D1Bitmap1> targetBitmap;
    if (FAILED(d2dContext_->CreateBitmapFromDxgiSurface(
            backBuffer.Get(), &bitmapProperties, targetBitmap.ReleaseAndGetAddressOf()))) {
        return false;
    }
    d2dContext_->SetTarget(targetBitmap.Get());
    d2dContext_->SetDpi(dpiX_, dpiY_);

    if (!dwriteFactory_) {
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf())))) {
            return false;
        }
    }

    return true;
}

void RenderDevice::ReleaseDeviceResources() {
    if (d2dContext_) {
        d2dContext_->SetTarget(nullptr);
    }
    d2dContext_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    swapChain_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();
}

void RenderDevice::Resize(UINT width, UINT height) {
    if (!swapChain_ || width == 0 || height == 0) {
        return;
    }
    d2dContext_->SetTarget(nullptr);

    if (FAILED(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
        CreateDeviceResources(hwnd_);
        return;
    }

    ComPtr<IDXGISurface> backBuffer;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf()));
    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpiX_, dpiY_);
    ComPtr<ID2D1Bitmap1> targetBitmap;
    d2dContext_->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bitmapProperties,
                                              targetBitmap.ReleaseAndGetAddressOf());
    d2dContext_->SetTarget(targetBitmap.Get());
}

void RenderDevice::SetDpi(float dpiX, float dpiY) {
    dpiX_ = dpiX;
    dpiY_ = dpiY;
    if (d2dContext_) {
        d2dContext_->SetDpi(dpiX_, dpiY_);
    }
}

ID2D1DeviceContext* RenderDevice::BeginDraw() {
    if (!d2dContext_) {
        return nullptr;
    }
    d2dContext_->BeginDraw();
    return d2dContext_.Get();
}

bool RenderDevice::EndDraw() {
    HRESULT hr = d2dContext_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        CreateDeviceResources(hwnd_);
        return false;
    }
    hr = swapChain_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        CreateDeviceResources(hwnd_);
        return false;
    }
    return true;
}

}  // namespace tw::app
```

- [ ] **Step 3: Wire `RenderDevice` into `MainWindow`**

Modify `src/app/shell/MainWindow.h` — add the include and member, add `Paint`:

```cpp
// src/app/shell/MainWindow.h
#pragma once

#include "pch.h"
#include "render/RenderDevice.h"

namespace tw::app {

class MainWindow {
public:
    bool Create(HINSTANCE instance, int cmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Paint();

    HWND hwnd_ = nullptr;
    RenderDevice renderDevice_;
};

}  // namespace tw::app
```

Modify `src/app/shell/MainWindow.cpp` — initialize the device after window creation, and handle `WM_PAINT`/`WM_SIZE`/`WM_DPICHANGED`:

```cpp
// src/app/shell/MainWindow.cpp
#include "pch.h"
#include "shell/MainWindow.h"

namespace tw::app {

namespace {
constexpr wchar_t kClassName[] = L"TracewellMainWindow";
}

bool MainWindow::Create(HINSTANCE instance, int cmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    hwnd_ = CreateWindowExW(0, kClassName, L"Tracewell", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720,
                             nullptr, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }

    if (!renderDevice_.Initialize(hwnd_)) {
        return false;
    }

    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            Paint();
            return 0;
        case WM_SIZE:
            renderDevice_.Resize(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            renderDevice_.SetDpi(static_cast<float>(LOWORD(wParam)),
                                  static_cast<float>(HIWORD(wParam)));
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::Paint() {
    ID2D1DeviceContext* context = renderDevice_.BeginDraw();
    if (!context) {
        ValidateRect(hwnd_, nullptr);
        return;
    }
    context->Clear(D2D1::ColorF(0.95f, 0.95f, 0.95f));
    if (!renderDevice_.EndDraw()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    ValidateRect(hwnd_, nullptr);
}

}  // namespace tw::app
```

- [ ] **Step 4: Add new files to `App.vcxproj`**

Add `<ClInclude Include="render\RenderDevice.h" />` next to the existing `ClInclude` entries, and `<ClCompile Include="render\RenderDevice.cpp" />` next to the existing `ClCompile` entries.

- [ ] **Step 5: Build and verify**

Run the same PowerShell msbuild command as Task 3, Step 7.
Expected: `Build succeeded. 0 Error(s)`.

- [ ] **Step 6: Manual smoke check**

Run `App.exe`. Expected: window shows a light-gray filled background (proof the D3D11/DXGI/D2D pipeline renders), resizing the window doesn't crash or show garbage.

- [ ] **Step 7: Commit**

```bash
git add src/app/render src/app/shell src/app/App.vcxproj
git commit -m "app: add RenderDevice (DXGI swap chain + D2D1DeviceContext) and wire into MainWindow"
```

---

### Task 5: Widget toolkit (`Theme`, `Widget`, `Panel`, `Button`, `TextBlock`)

**Files:**
- Create: `src/app/ui/Theme.h`, `src/app/ui/Theme.cpp`
- Create: `src/app/ui/Widget.h`
- Create: `src/app/ui/Panel.h`, `src/app/ui/Panel.cpp`
- Create: `src/app/ui/Button.h`, `src/app/ui/Button.cpp`
- Create: `src/app/ui/TextBlock.h`, `src/app/ui/TextBlock.cpp`
- Create: `src/app/ui/ListView.h`, `src/app/ui/ListView.cpp`
- Modify: `src/app/shell/MainWindow.h`, `src/app/shell/MainWindow.cpp`
- Modify: `src/app/App.vcxproj`

**Interfaces:**
- Produces: `tw::app::ThemeColors`, `tw::app::ThemeMode`, `tw::app::Theme::DetectSystemTheme()`, `tw::app::Theme::ColorsFor(ThemeMode)`; `tw::app::Widget` base (`SetBounds`, `Bounds`, `HitTest`, `Draw`, `Invalidate`, `IsDirty`, `ClearDirty`); `tw::app::Panel` (`AddChild`, `Layout`, `HitTestChildren`); `tw::app::Button` (`SetText`, `SetOnClick`, `SetHovered`, `Click`); `tw::app::TextBlock` (`SetText`); `tw::app::ListView` (`SetColumns`, `SetRows`, `HitTestHeader`).
- Consumes: `RenderDevice::DWriteFactory()` from Task 4.

Note: `ListView` is built to satisfy the spec's widget-toolkit scope and Definition of Done (`docs/superpowers/specs/2026-07-24-win32-d2d-ui-framework-design.md`), but the Startup validating screen (Task 6) only needs a count via `TextBlock` — `ListView` is not wired into a screen in this plan. It becomes the first real consumer when the Task Scheduler/Services sources (Phase 1.2-1.3) or the Disk I/O screen (Phase 1.6) are built as their own subprojects.

- [ ] **Step 1: Write `ui/Theme.h` and `ui/Theme.cpp`**

```cpp
// src/app/ui/Theme.h
#pragma once

#include "pch.h"

namespace tw::app {

struct ThemeColors {
    D2D1_COLOR_F background;
    D2D1_COLOR_F surface;
    D2D1_COLOR_F text;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F border;
};

enum class ThemeMode { Light, Dark };

// Таблицы цветов тёмной/светлой темы + чтение текущей системной темы из реестра.
class Theme {
public:
    static ThemeMode DetectSystemTheme();
    static const ThemeColors& ColorsFor(ThemeMode mode);
};

}  // namespace tw::app
```

```cpp
// src/app/ui/Theme.cpp
#include "pch.h"
#include "ui/Theme.h"

namespace tw::app {

namespace {
constexpr ThemeColors kLight{
    {0.96f, 0.96f, 0.96f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {0.10f, 0.10f, 0.10f, 1.0f},
    {0.0f, 0.47f, 0.84f, 1.0f},
    {0.82f, 0.82f, 0.82f, 1.0f},
};
constexpr ThemeColors kDark{
    {0.11f, 0.11f, 0.12f, 1.0f},
    {0.16f, 0.16f, 0.18f, 1.0f},
    {0.93f, 0.93f, 0.94f, 1.0f},
    {0.20f, 0.60f, 0.95f, 1.0f},
    {0.27f, 0.27f, 0.29f, 1.0f},
};
}  // namespace

ThemeMode Theme::DetectSystemTheme() {
    DWORD value = 1;  // 1 = светлая тема, значение Windows по умолчанию
    DWORD size = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                       L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                       0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                          reinterpret_cast<BYTE*>(&value), &size);
        RegCloseKey(key);
    }
    return value == 0 ? ThemeMode::Dark : ThemeMode::Light;
}

const ThemeColors& Theme::ColorsFor(ThemeMode mode) {
    return mode == ThemeMode::Dark ? kDark : kLight;
}

}  // namespace tw::app
```

- [ ] **Step 2: Write `ui/Widget.h`**

```cpp
// src/app/ui/Widget.h
#pragma once

#include "pch.h"

namespace tw::app {

struct ThemeColors;

// База виджет-дерева: границы, hit-test, отрисовка, dirty-флаг.
class Widget {
public:
    virtual ~Widget() = default;

    void SetBounds(D2D1_RECT_F bounds) { bounds_ = bounds; Invalidate(); }
    const D2D1_RECT_F& Bounds() const { return bounds_; }

    virtual bool HitTest(D2D1_POINT_2F point) const {
        return point.x >= bounds_.left && point.x <= bounds_.right &&
               point.y >= bounds_.top && point.y <= bounds_.bottom;
    }

    virtual void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
                       const ThemeColors& theme) = 0;

    void Invalidate() { dirty_ = true; }
    bool IsDirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

protected:
    D2D1_RECT_F bounds_{0, 0, 0, 0};
    bool dirty_ = true;
};

}  // namespace tw::app
```

- [ ] **Step 3: Write `ui/Panel.h` and `ui/Panel.cpp`**

```cpp
// src/app/ui/Panel.h
#pragma once

#include <memory>
#include <vector>

#include "pch.h"
#include "ui/Widget.h"

namespace tw::app {

enum class PanelOrientation { Vertical, Horizontal };

// Простой stack-layout: располагает детей друг за другом с фиксированным отступом.
class Panel : public Widget {
public:
    explicit Panel(PanelOrientation orientation, float spacing = 8.0f)
        : orientation_(orientation), spacing_(spacing) {}

    void AddChild(std::shared_ptr<Widget> child, float size);
    void Layout();
    Widget* HitTestChildren(D2D1_POINT_2F point) const;

    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
              const ThemeColors& theme) override;

private:
    struct Entry {
        std::shared_ptr<Widget> widget;
        float size;
    };

    PanelOrientation orientation_;
    float spacing_;
    std::vector<Entry> children_;
};

}  // namespace tw::app
```

```cpp
// src/app/ui/Panel.cpp
#include "pch.h"
#include "ui/Panel.h"

namespace tw::app {

void Panel::AddChild(std::shared_ptr<Widget> child, float size) {
    children_.push_back({std::move(child), size});
    Invalidate();
}

void Panel::Layout() {
    float cursor = orientation_ == PanelOrientation::Vertical ? bounds_.top : bounds_.left;
    for (auto& entry : children_) {
        D2D1_RECT_F childBounds = bounds_;
        if (orientation_ == PanelOrientation::Vertical) {
            childBounds.top = cursor;
            childBounds.bottom = cursor + entry.size;
            cursor = childBounds.bottom + spacing_;
        } else {
            childBounds.left = cursor;
            childBounds.right = cursor + entry.size;
            cursor = childBounds.right + spacing_;
        }
        entry.widget->SetBounds(childBounds);
    }
    Invalidate();
}

Widget* Panel::HitTestChildren(D2D1_POINT_2F point) const {
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (it->widget->HitTest(point)) {
            return it->widget.get();
        }
    }
    return nullptr;
}

void Panel::Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
                  const ThemeColors& theme) {
    for (auto& entry : children_) {
        entry.widget->Draw(context, dwriteFactory, theme);
        entry.widget->ClearDirty();
    }
    ClearDirty();
}

}  // namespace tw::app
```

- [ ] **Step 4: Write `ui/TextBlock.h` and `ui/TextBlock.cpp`**

```cpp
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
```

```cpp
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
```

- [ ] **Step 5: Write `ui/Button.h` and `ui/Button.cpp`**

```cpp
// src/app/ui/Button.h
#pragma once

#include <functional>
#include <string>

#include "pch.h"
#include "ui/Widget.h"

namespace tw::app {

class Button : public Widget {
public:
    void SetText(std::wstring text) { text_ = std::move(text); Invalidate(); }
    void SetOnClick(std::function<void()> handler) { onClick_ = std::move(handler); }

    void SetHovered(bool hovered);
    void Click();

    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory,
              const ThemeColors& theme) override;

private:
    std::wstring text_;
    std::function<void()> onClick_;
    bool hovered_ = false;
};

}  // namespace tw::app
```

```cpp
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
```

- [ ] **Step 6: Write `ui/ListView.h` and `ui/ListView.cpp`**

```cpp
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
```

```cpp
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
```

- [ ] **Step 7: Wire a placeholder screen into `MainWindow` to visually prove the toolkit**

Modify `src/app/shell/MainWindow.h`:

```cpp
// src/app/shell/MainWindow.h
#pragma once

#include <memory>

#include "pch.h"
#include "render/RenderDevice.h"
#include "ui/Button.h"
#include "ui/TextBlock.h"
#include "ui/Theme.h"

namespace tw::app {

class MainWindow {
public:
    bool Create(HINSTANCE instance, int cmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Paint();
    void HandleMouseMove(D2D1_POINT_2F point);
    void HandleLeftButtonUp(D2D1_POINT_2F point);

    HWND hwnd_ = nullptr;
    RenderDevice renderDevice_;
    ThemeMode themeMode_ = ThemeMode::Light;

    std::shared_ptr<Button> helloButton_;
    std::shared_ptr<TextBlock> helloText_;
};

}  // namespace tw::app
```

Modify `src/app/shell/MainWindow.cpp` (add `#include <windowsx.h>`, `#include "ui/Theme.h"` at top; extend `Create` and `HandleMessage`; replace `Paint`):

```cpp
// src/app/shell/MainWindow.cpp
#include "pch.h"
#include "shell/MainWindow.h"

#include <windowsx.h>

using Microsoft::WRL::ComPtr;

namespace tw::app {

namespace {
constexpr wchar_t kClassName[] = L"TracewellMainWindow";
}

bool MainWindow::Create(HINSTANCE instance, int cmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    hwnd_ = CreateWindowExW(0, kClassName, L"Tracewell", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720,
                             nullptr, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }

    if (!renderDevice_.Initialize(hwnd_)) {
        return false;
    }
    themeMode_ = Theme::DetectSystemTheme();

    helloButton_ = std::make_shared<Button>();
    helloButton_->SetText(L"Hello");
    helloButton_->SetBounds(D2D1::RectF(24, 24, 144, 64));

    helloText_ = std::make_shared<TextBlock>();
    helloText_->SetText(L"Tracewell");
    helloText_->SetBounds(D2D1::RectF(24, 80, 300, 104));

    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            Paint();
            return 0;
        case WM_SIZE:
            renderDevice_.Resize(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            renderDevice_.SetDpi(static_cast<float>(LOWORD(wParam)),
                                  static_cast<float>(HIWORD(wParam)));
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SETTINGCHANGE:
            themeMode_ = Theme::DetectSystemTheme();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            HandleMouseMove(D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lParam)),
                                           static_cast<float>(GET_Y_LPARAM(lParam))));
            return 0;
        case WM_LBUTTONUP:
            HandleLeftButtonUp(D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lParam)),
                                              static_cast<float>(GET_Y_LPARAM(lParam))));
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::HandleMouseMove(D2D1_POINT_2F point) {
    bool hovered = helloButton_->HitTest(point);
    helloButton_->SetHovered(hovered);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::HandleLeftButtonUp(D2D1_POINT_2F point) {
    if (helloButton_->HitTest(point)) {
        helloButton_->Click();
    }
}

void MainWindow::Paint() {
    ID2D1DeviceContext* context = renderDevice_.BeginDraw();
    if (!context) {
        ValidateRect(hwnd_, nullptr);
        return;
    }
    const ThemeColors& theme = Theme::ColorsFor(themeMode_);
    context->Clear(theme.background);
    helloButton_->Draw(context, renderDevice_.DWriteFactory(), theme);
    helloText_->Draw(context, renderDevice_.DWriteFactory(), theme);
    if (!renderDevice_.EndDraw()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    ValidateRect(hwnd_, nullptr);
}

}  // namespace tw::app
```

- [ ] **Step 8: Add new files to `App.vcxproj`**

Add `ClInclude` entries for `ui\Theme.h`, `ui\Widget.h`, `ui\Panel.h`, `ui\Button.h`, `ui\TextBlock.h`, `ui\ListView.h`, and `ClCompile` entries for `ui\Theme.cpp`, `ui\Panel.cpp`, `ui\Button.cpp`, `ui\TextBlock.cpp`, `ui\ListView.cpp`.

- [ ] **Step 9: Build and verify**

Run the same PowerShell msbuild command as Task 3, Step 7.
Expected: `Build succeeded. 0 Error(s)` (this includes `ListView.cpp` compiling cleanly even though nothing instantiates `ListView` yet).

- [ ] **Step 10: Manual smoke check**

Run `App.exe`. Expected: themed background (light or dark matching current Windows setting), a rounded "Hello" button that highlights on hover, and a "Tracewell" text label below it.

- [ ] **Step 11: Commit**

```bash
git add src/app/ui src/app/shell src/app/App.vcxproj
git commit -m "app: add widget toolkit (Theme, Widget, Panel, Button, TextBlock, ListView) with a placeholder screen"
```

---

### Task 6: MVVM wiring — real Startup screen with sidebar navigation

**Files:**
- Create: `src/app/mvvm/ViewModel.h`
- Create: `src/app/mvvm/MainViewModel.h`, `src/app/mvvm/MainViewModel.cpp`
- Modify: `src/app/shell/MainWindow.h`, `src/app/shell/MainWindow.cpp` (replace placeholder from Task 5 with the real layout)
- Modify: `src/app/App.vcxproj`

**Interfaces:**
- Consumes: `tw::app::Property<T>` (Task 1), `tw::app::Dispatcher` (Task 2), `tw::app::Panel`/`Button`/`TextBlock`/`Theme` (Task 5), `tw::StartupRegistryCollector`/`tw::CancellationToken`/`tw::CollectorResult` (`src/core/collectors/startup_registry.h`, `src/core/collector.h`).
- Produces: `tw::app::MainViewModel` — `explicit MainViewModel(Dispatcher& dispatcher)`, `Property<std::wstring>& StatusText()`, `void Refresh()`.

- [ ] **Step 1: Write `mvvm/ViewModel.h`**

```cpp
// src/app/mvvm/ViewModel.h
#pragma once

namespace tw::app {

class ViewModel {
public:
    virtual ~ViewModel() = default;
};

}  // namespace tw::app
```

- [ ] **Step 2: Write `mvvm/MainViewModel.h`**

```cpp
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
```

- [ ] **Step 3: Write `mvvm/MainViewModel.cpp`**

```cpp
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
```

- [ ] **Step 4: Replace the placeholder screen in `MainWindow` with sidebar + Startup content**

Modify `src/app/shell/MainWindow.h`:

```cpp
// src/app/shell/MainWindow.h
#pragma once

#include <memory>

#include "pch.h"
#include "dispatch/Dispatcher.h"
#include "mvvm/MainViewModel.h"
#include "render/RenderDevice.h"
#include "ui/Button.h"
#include "ui/Panel.h"
#include "ui/TextBlock.h"
#include "ui/Theme.h"

namespace tw::app {

constexpr UINT WM_APP_DISPATCH = WM_APP + 1;

class MainWindow {
public:
    bool Create(HINSTANCE instance, int cmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void BuildLayout();
    void Paint();
    void HandleMouseMove(D2D1_POINT_2F point);
    void HandleLeftButtonUp(D2D1_POINT_2F point);

    HWND hwnd_ = nullptr;
    RenderDevice renderDevice_;
    ThemeMode themeMode_ = ThemeMode::Light;

    Dispatcher dispatcher_{[this] { PostMessageW(hwnd_, WM_APP_DISPATCH, 0, 0); }};
    MainViewModel viewModel_{dispatcher_};

    std::shared_ptr<Panel> sidebar_;
    std::shared_ptr<Panel> content_;
    std::shared_ptr<Button> refreshButton_;
    std::shared_ptr<TextBlock> statusText_;
    Button* hoveredButton_ = nullptr;
};

}  // namespace tw::app
```

Modify `src/app/shell/MainWindow.cpp` in full:

```cpp
// src/app/shell/MainWindow.cpp
#include "pch.h"
#include "shell/MainWindow.h"

#include <windowsx.h>

using Microsoft::WRL::ComPtr;

namespace tw::app {

namespace {
constexpr wchar_t kClassName[] = L"TracewellMainWindow";
constexpr float kSidebarWidth = 200.0f;
}  // namespace

bool MainWindow::Create(HINSTANCE instance, int cmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    hwnd_ = CreateWindowExW(0, kClassName, L"Tracewell", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720,
                             nullptr, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }

    if (!renderDevice_.Initialize(hwnd_)) {
        return false;
    }
    themeMode_ = Theme::DetectSystemTheme();
    BuildLayout();

    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
    return true;
}

void MainWindow::BuildLayout() {
    sidebar_ = std::make_shared<Panel>(PanelOrientation::Vertical, 4.0f);

    auto startupEntry = std::make_shared<Button>();
    startupEntry->SetText(L"Startup");
    sidebar_->AddChild(startupEntry, 40.0f);

    // Заглушка будущего раздела: SetOnClick не задан, поэтому клик не даёт эффекта.
    auto diskEntry = std::make_shared<Button>();
    diskEntry->SetText(L"Disk I/O (скоро)");
    sidebar_->AddChild(diskEntry, 40.0f);

    content_ = std::make_shared<Panel>(PanelOrientation::Vertical, 12.0f);

    refreshButton_ = std::make_shared<Button>();
    refreshButton_->SetText(L"Refresh");
    refreshButton_->SetOnClick([this] { viewModel_.Refresh(); });
    content_->AddChild(refreshButton_, 40.0f);

    statusText_ = std::make_shared<TextBlock>();
    statusText_->SetText(viewModel_.StatusText().Get());
    content_->AddChild(statusText_, 24.0f);

    viewModel_.StatusText().Subscribe([this](const std::wstring& text) {
        statusText_->SetText(text);
        InvalidateRect(hwnd_, nullptr, FALSE);
    });
}

int MainWindow::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_APP_DISPATCH:
            dispatcher_.DrainUiQueue();
            return 0;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_SIZE: {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            renderDevice_.Resize(width, height);
            sidebar_->SetBounds(D2D1::RectF(0, 0, kSidebarWidth, static_cast<float>(height)));
            sidebar_->Layout();
            content_->SetBounds(D2D1::RectF(kSidebarWidth + 16.0f, 16.0f,
                                             static_cast<float>(width) - 16.0f,
                                             static_cast<float>(height) - 16.0f));
            content_->Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DPICHANGED: {
            renderDevice_.SetDpi(static_cast<float>(LOWORD(wParam)),
                                  static_cast<float>(HIWORD(wParam)));
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SETTINGCHANGE:
            themeMode_ = Theme::DetectSystemTheme();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            HandleMouseMove(D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lParam)),
                                           static_cast<float>(GET_Y_LPARAM(lParam))));
            return 0;
        case WM_LBUTTONUP:
            HandleLeftButtonUp(D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lParam)),
                                              static_cast<float>(GET_Y_LPARAM(lParam))));
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::HandleMouseMove(D2D1_POINT_2F point) {
    Button* hit = refreshButton_->HitTest(point) ? refreshButton_.get() : nullptr;
    if (hit != hoveredButton_) {
        if (hoveredButton_) hoveredButton_->SetHovered(false);
        if (hit) hit->SetHovered(true);
        hoveredButton_ = hit;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::HandleLeftButtonUp(D2D1_POINT_2F point) {
    if (refreshButton_->HitTest(point)) {
        refreshButton_->Click();
    }
}

void MainWindow::Paint() {
    ID2D1DeviceContext* context = renderDevice_.BeginDraw();
    if (!context) {
        ValidateRect(hwnd_, nullptr);
        return;
    }

    const ThemeColors& theme = Theme::ColorsFor(themeMode_);
    context->Clear(theme.background);

    ComPtr<ID2D1SolidColorBrush> sidebarBrush;
    context->CreateSolidColorBrush(theme.surface, sidebarBrush.ReleaseAndGetAddressOf());
    context->FillRectangle(sidebar_->Bounds(), sidebarBrush.Get());

    sidebar_->Draw(context, renderDevice_.DWriteFactory(), theme);
    content_->Draw(context, renderDevice_.DWriteFactory(), theme);

    if (!renderDevice_.EndDraw()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    ValidateRect(hwnd_, nullptr);
}

}  // namespace tw::app
```

- [ ] **Step 5: Add new files to `App.vcxproj`**

Add `ClInclude` entries for `mvvm\ViewModel.h`, `mvvm\MainViewModel.h`, `mvvm\Property.h`, `dispatch\Dispatcher.h`; add `ClCompile` entries for `mvvm\MainViewModel.cpp` and `dispatch\Dispatcher.cpp` (the latter with `<PrecompiledHeader>NotUsing</PrecompiledHeader>` so it stays buildable standalone under CMake too, matching Task 2):

```xml
<ClCompile Include="dispatch\Dispatcher.cpp">
  <PrecompiledHeader>NotUsing</PrecompiledHeader>
</ClCompile>
```

- [ ] **Step 6: Build and verify**

Run the same PowerShell msbuild command as Task 3, Step 7.
Expected: `Build succeeded. 0 Error(s)`.

- [ ] **Step 7: Manual smoke check — end-to-end Refresh**

Run `App.exe`. Click "Refresh". Expected: status text briefly shows "Загрузка...", then updates to "Найдено записей автозагрузки: N". Cross-check N against:

```powershell
build\src\Release\tracewell-cli.exe snapshot --collector startup.registry
```
(or equivalent existing CLI invocation — the count must match). Confirm the sidebar shows "Startup" active and "Disk I/O (скоро)" as a non-functional placeholder (clicking it does nothing).

- [ ] **Step 8: Commit**

```bash
git add src/app/mvvm src/app/dispatch src/app/shell src/app/App.vcxproj
git commit -m "app: wire MVVM + Dispatcher into a real Startup screen with sidebar nav"
```

---

### Task 7: Final verification pass and spec close-out

**Files:**
- Modify: `docs/superpowers/specs/2026-07-24-win32-d2d-ui-framework-design.md` (check off Definition of Done)

**Interfaces:** none — this task only verifies and documents.

- [ ] **Step 1: Run the full unit test suite**

Run: `cmake --build build --config Release --target tracewell-tests && ctest --test-dir build -C Release --output-on-failure`
Expected: all tests pass, including the 5 new `Property`/`Dispatcher` cases from Tasks 1–2.

- [ ] **Step 2: DPI smoke check**

In Windows Display Settings, set scaling to 100%, then 150%, then 200% (or use `App.exe` on monitors with different scaling if available). Relaunch/move `App.exe` between each. Expected: no crash, no visibly clipped/blurry text, layout stays proportionate at each scale.

- [ ] **Step 3: Theme smoke check**

Toggle Windows Settings → Personalization → Colors → "Choose your mode" between Light and Dark while `App.exe` is running. Expected: window repaints with the new theme's colors within one redraw (no restart needed).

- [ ] **Step 4: Code-review invariant check**

Read through `src/app/mvvm/MainViewModel.cpp::Refresh()` and confirm by inspection: the only place `tw::StartupRegistryCollector`/`tw::CancellationToken` is touched is inside the `dispatcher_.Submit<std::wstring>` worker lambda — never in `MainWindow`, never in the `onComplete` lambda, never on `WM_APP_DISPATCH`/`WM_LBUTTONUP` handling.

- [ ] **Step 5: Update the spec's Definition of Done**

Edit `docs/superpowers/specs/2026-07-24-win32-d2d-ui-framework-design.md`, checking off all 8 boxes under "## Definition of Done" (they should now all be true given Steps 1-4 above passed).

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-07-24-win32-d2d-ui-framework-design.md
git commit -m "docs: close out Win32+Direct2D UI framework Definition of Done"
```
