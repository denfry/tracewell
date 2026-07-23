# WinUI 3 PoC — дизайн

Дата: 2026-07-23
Подпроект Phase 1 (`docs/02-phase-1-mvp.md`, раздел 1.1): первый шаг MVP-фазы, снимает риск «WinUI 3 deployment/стабильность» до того, как на UI-фреймворке начнут строиться реальные экраны.

## Цель

Минимальный, но архитектурно честный WinUI 3 скелет, который доказывает три вещи одновременно:

1. C++/WinRT + WinUI 3 проект собирается рядом с существующим CMake-деревом (`src/core`, `src/cli`).
2. Self-contained unpackaged deployment запускается без установленного в системе Windows App SDK runtime.
3. Core↔UI граница работает по спецификации: core выполняется в фоновом потоке, UI получает результат только через `DispatcherQueue`, ни один WinAPI/collector-вызов не происходит в UI-потоке.

Если PoC не проходит (например, WinUI3 деплой нестабилен) — фиксируется откат на fallback из спеки Phase 1 (Win32 + Direct2D), зафиксированный отдельным ADR.

## Границы (Scope)

**Входит:**
- `src/app/App.vcxproj` — отдельный MSBuild-проект C++/WinRT + WinUI 3, unpackaged self-contained.
- Одно окно (`MainWindow`) с `TextBlock` и кнопкой «Refresh».
- Минимальный MVVM: `MainViewModel` со свойством `StatusText`, без XAML data-binding инфраструктуры сверх необходимого.
- Асинхронный вызов существующего `tw::StartupRegistryCollector` (`src/core/collectors/startup_registry.h`) из фонового потока с возвратом на UI через `DispatcherQueue`.
- Линковка `App.vcxproj` на `tracewell-core.lib`, собранную CMake-пресетом `default`.

**Не входит (следующие подпроекты):**
- Task Scheduler / Services collectors на экране — только `startup.registry`.
- Навигация между экранами, финальная XAML-стилизация, тема/DPI-полировка.
- MSIX/WiX инсталлятор (раздел 1.7 Phase 1) — deployment-режим этого PoC (unpackaged) не блокирует последующий выбор инсталлятора.
- Elevated helper, enable/disable автозагрузки, ActionJournal.

## Архитектура

```
src/
  core/            (CMake, tracewell-core — без изменений)
  cli/              (CMake, tracewell-cli — без изменений)
  app/              (новое, MSBuild)
    App.vcxproj
    App.xaml / App.cpp / App.h
    MainWindow.xaml / MainWindow.cpp / MainWindow.h
    MainViewModel.h / .cpp
```

- `App.vcxproj` — самостоятельный MSBuild-проект, не часть `CMakeLists.txt` дерева. Инклюды на `src/` (`AdditionalIncludeDirectories`), линковка на `tracewell-core.lib` из CMake build-директории (путь задаётся через property sheet, при смене build-директории обновляется вручную — авто-обнаружение вне scope PoC).
- Поток вызова:
  1. Пользователь жмёт «Refresh».
  2. `MainViewModel::RefreshAsync()` — корутина C++/WinRT.
  3. `co_await winrt::resume_background()` — уход с UI-потока.
  4. `tw::CancellationToken token; auto result = tw::StartupRegistryCollector{}.collect(token);` — вызов core-коллектора в фоновом потоке.
  5. `co_await dispatcherQueue.resume_foreground()` (или эквивалент через `DispatcherQueue::TryEnqueue`) — возврат на UI-поток.
  6. Обновление `MainViewModel::StatusText` (например, "Найдено записей автозагрузки: N") → биндинг обновляет `TextBlock`.
- Инвариант, который PoC обязан продемонстрировать: ни один вызов `tw::*` или WinAPI не происходит между шагами 1 и 3, либо после шага 5 — вся работа с core строго внутри фонового участка.

## Deployment

Unpackaged, self-contained (Windows App SDK runtime бандлится в выходную директорию сборки).

Обоснование:
- Самый простой путь для этого шага — просто `.exe`, без MSIX-манифеста и подписи.
- Сама спека Phase 1 (раздел «Открытые вопросы») отмечает, что MSIX осложняет elevated helper (packaged full-trust). Unpackaged лучше согласуется с elevation-архитектурой раздела 1.5 (`tracewell-elevated.exe` через `ShellExecuteExW runas`).
- Финальный инсталлятор (MSIX vs WiX MSI, раздел 1.7) — отдельный подпроект, этим решением не блокируется и не предрешается.

## Тестирование

Ручная проверка (unit-тестов для чистого UI-skeleton не заводим — нечего тестировать автоматически на этом шаге):
- Release-сборка `App.vcxproj` проходит без ошибок.
- `App.exe` запускается из output-директории; при возможности — на машине без установленного Windows App SDK runtime (подтверждает self-contained).
- После «Refresh» `StatusText` показывает реальное количество записей автозагрузки, совпадающее с выводом `tracewell-cli snapshot` для того же источника.
- Проверка (по коду/логам сборки), что core-вызов действительно происходит в фоновом потоке, а не синхронно в UI-потоке.

## Definition of Done

- [ ] `src/app/App.vcxproj` собирается в Release, линкуется на `tracewell-core.lib`.
- [ ] Self-contained unpackaged `App.exe` запускается без установленного WASDK runtime.
- [ ] Нажатие «Refresh» асинхронно (фон → `DispatcherQueue`) отображает число записей из `StartupRegistryCollector`.
- [ ] Ни одного WinAPI/collector-вызова в UI-потоке (проверено по коду).
- [ ] Решение зафиксировано: идём дальше с WinUI 3, либо документирован откат на Win32+D2D (ADR).
