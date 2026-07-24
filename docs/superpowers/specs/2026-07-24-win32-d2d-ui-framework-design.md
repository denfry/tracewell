# Win32 + Direct2D UI Framework — дизайн

Дата: 2026-07-24
Подпроект Phase 1 (`docs/02-phase-1-mvp.md`, раздел 1.1): выбор UI-фреймворка после отката с WinUI 3 (см. [ADR 0001](../../adr/0001-winui3-poc-fallback-to-win32-d2d.md)). Строит фундамент — окно, виджет-тулкит, диспетчер фоновых задач, MVVM-инфраструктуру — на котором позже будут построены экраны Startup (полностью) и Disk I/O (Phase 1.6, отдельный подпроект).

## Цель

Рабочий каркас Win32 + Direct2D приложения, который доказывает те же три вещи, что были целью WinUI 3 PoC, плюс закрывает архитектурный пробел (отсутствие XAML) собственным минимальным тулкитом:

1. `src/app` (переписанный под MSBuild vcxproj, Win32 + Direct2D) собирается и линкуется на `tracewell-core.lib`.
2. Self-contained unpackaged `.exe` запускается без внешних рантайм-зависимостей (Windows App SDK не используется вовсе).
3. Core↔UI граница работает по инварианту спеки: core выполняется в фоновом потоке пула диспетчера, UI получает результат только через сообщение `WM_APP_DISPATCH` в главном цикле сообщений; ни один WinAPI/collector-вызов не происходит в UI-потоке.
4. Минимальный retained-mode виджет-тулкит (взамен XAML) достаточен для построения экрана Startup и задела на будущие экраны без дублирования hit-testing/redraw-логики в каждом экране.

## Границы (Scope)

**Входит:**
- `src/app/App.vcxproj` — MSBuild-проект, Win32 + Direct2D, unpackaged self-contained (без Windows App SDK).
- Главное окно: message loop, DPI-awareness (per-monitor v2 манифест), DXGI swap chain + `ID2D1DeviceContext`.
- Виджет-тулкит: `Widget` (база), `Button`, `TextBlock`, `ListView` (простой список строк с сортировкой колонок), `Panel` (stack/grid layout).
- `Theme` — таблицы цветов/метрик для тёмной/светлой темы, переключение по `WM_SETTINGCHANGE`.
- MVVM-инфраструктура: `Property<T>` (observable-обёртка с подпиской на изменения), базовый `ViewModel`.
- `Dispatcher`: пул фоновых потоков (2–4) + потокобезопасная очередь результатов + доставка на UI-поток через `PostMessage`/`WM_APP_DISPATCH`.
- Sidebar-навигация с одним реальным разделом («Startup») и заглушками для будущих разделов (Disk I/O и др. — неактивны, без экрана за ними).
- Экран Startup: кнопка «Refresh», асинхронный вызов существующего `tw::StartupRegistryCollector` из фонового потока пула, отображение количества записей через `Property<T>` → `TextBlock`.

**Не входит (следующие подпроекты):**
- Экран Disk I/O (Phase 1.6).
- Остальные источники автозагрузки (Task Scheduler, Services — Phase 1.2–1.3), enable/disable, `ActionJournal` (Phase 1.5).
- Elevated helper, MSIX/WiX инсталлятор (Phase 1.7).
- Полная навигация между реальными экранами (только заглушки в sidebar — сама навигационная механика в тулките присутствует и проверяется на заглушках).

## Архитектура

```
src/
  core/            (CMake, tracewell-core — без изменений)
  cli/              (CMake, tracewell-cli — без изменений)
  app/              (переписывается: MSBuild, Win32 + Direct2D)
    main.cpp                    — WinMain, message loop, создание окна, DPI-манифест
    render/
      RenderDevice.h/.cpp        — D3D11 device + DXGI swap chain + ID2D1DeviceContext, resize/DPI-change
    ui/
      Widget.h                   — база: bounds, hit-test, draw(ID2D1DeviceContext&), invalidate
      Button.h/.cpp
      TextBlock.h/.cpp
      ListView.h/.cpp
      Panel.h/.cpp
      Theme.h/.cpp
    mvvm/
      Property.h                 — Property<T>: get/set + подписка onChanged
      ViewModel.h                 — база
      MainViewModel.h/.cpp         — StatusText, RefreshAsync()
    dispatch/
      Dispatcher.h/.cpp            — пул потоков + очередь результатов + WM_APP_DISPATCH
    shell/
      MainWindow.h/.cpp             — sidebar nav + content area, композиция виджет-деревьев по разделам
```

- `App.vcxproj` — самостоятельный MSBuild-проект (как и WinUI3-версия), инклюды на `src/`, линковка на `tracewell-core.lib` из CMake build-директории (путь через property sheet, как в предыдущем PoC).
- Deployment: unpackaged, self-contained в смысле «без внешних рантайм-зависимостей» — Direct2D/DirectWrite/DXGI/D3D11 являются частью ОС (начиная с Windows 8+), отдельного рантайма для установки не требуется. Это проще, чем self-contained WASDK deployment из WinUI3-версии.

### Поток вызова (экран Startup)

1. `WinMain` создаёт окно, `RenderDevice`, `Dispatcher`, `MainViewModel`.
2. Sidebar показывает «Startup» (активный) + заглушки будущих разделов (не кликабельны).
3. Пользователь жмёт «Refresh» (`Button`-виджет) → `MainViewModel::Refresh()` вызывает `Dispatcher::Submit(task)`.
4. Поток пула выполняет `tw::StartupRegistryCollector{}.collect(token)` — WinAPI/collector-вызовы происходят исключительно здесь; граница пересекается только через `Dispatcher::Submit`.
5. Поток пула кладёт результат в потокобезопасную очередь и вызывает `PostMessage(hwnd, WM_APP_DISPATCH, ...)`.
6. Главный цикл сообщений на `WM_APP_DISPATCH` вычитывает очередь на UI-потоке, вызывает callback завершения, который выставляет `MainViewModel::StatusText` — `Property<T>` уведомляет подписчиков.
7. Подписанный `TextBlock` помечает себя dirty; следующий `WM_PAINT` перерисовывает только изменённое (без полного релэйаута дерева).

### Rendering

DXGI swap chain (`DXGI_SWAP_EFFECT_FLIP_DISCARD`) + D3D11 device + `ID2D1DeviceContext`, созданный из DXGI-поверхности. `WM_DPICHANGED` и `ResizeBuffers` обрабатываются в `RenderDevice`. Текст — DirectWrite (`IDWriteTextFormat`/`IDWriteTextLayout`) для корректных DPI-масштабируемых метрик.

Обоснование выбора DXGI swap chain + `ID2D1DeviceContext` вместо `ID2D1HwndRenderTarget`: последний — устаревший API (Microsoft рекомендует новый путь для нового кода), хуже справляется с per-monitor DPI-изменениями и ресайзом, что напрямую конфликтует с DoD-требованием тестирования на 100/150/200% DPI.

### Виджет-тулкит

Retained-mode: каждый экран — дерево `Widget`. `Widget` знает свои `bounds`, умеет `HitTest(point)`, `Draw(deviceContext)`, `Invalidate()`. `Panel` — простой layout (vertical/horizontal stack + grid с фиксированными/пропорциональными колонками, без flex-подобной сложности — YAGNI для MVP-набора экранов). Виджеты подписываются на нужные им `Property<T>` конкретной ViewModel и вызывают `Invalidate()` при срабатывании callback — только затронутый виджет помечается dirty, не всё дерево.

### MVVM без XAML-биндинга

`Property<T>` — минимальная observable-обёртка: `T Get() const`, `void Set(T)`, `Subscription Subscribe(std::function<void(const T&)>)`. Явная подписка на конкретное свойство — вместо XAML `{Binding}` или полного re-render дерева при любом изменении. Выбор сделан осознанно: полный re-render дешевле в коде, но не подходит для будущего Disk I/O экрана (обновление раз в 1–2 сек, только часть виджетов меняется — sparkline/таблица, не весь layout).

### Диспетчер фоновых задач

`Dispatcher` — пул из 2–4 воркер-потоков, принимает задачи через `Submit(std::function<Result()> task, std::function<void(Result)> onComplete)`. Задача выполняется на воркере; результат кладётся в потокобезопасную очередь; `PostMessage(hwnd, WM_APP_DISPATCH, 0, 0)` будит главный цикл, который вычитывает очередь и вызывает `onComplete` строго на UI-потоке. Фиксированный пул вместо `std::thread` на каждый вызов — ограничивает конкурентность при частых действиях пользователя (enable/disable в будущих экранах) и убирает накладные расходы на создание потоков.

## Обработка ошибок

- Ошибки коллектора (например, отказ доступа к Run-ключу) отражаются как error-состояние `StatusText`, не крэш — тот же контракт, что уже есть в `tracewell-core`/CLI.
- Исключения в задачах `Dispatcher` перехватываются на границе задачи и передаются в `onComplete` как error-вариант результата; никогда не прорываются в message loop `WinMain`.
- Потеря устройства (`DXGI_ERROR_DEVICE_REMOVED`/`DXGI_ERROR_DEVICE_RESET`) — `RenderDevice` пересоздаётся при следующей отрисовке; стандартный паттерн для D3D/D2D, дёшево добавить сейчас, пока жизненный цикл устройства и так изолирован в одном классе.

## Тестирование

- **Unit** (в существующий CMake/ctest-набор, без GUI/D2D-зависимости): подписка/срабатывание `Property<T>`, логика вычитывания очереди `Dispatcher` (мокнутый message-pump).
- **Ручной smoke**:
  - Release-сборка `App.vcxproj` запускается unpackaged, без установленных дополнительных рантаймов.
  - «Refresh» показывает реальное количество записей автозагрузки, совпадающее с `tracewell-cli snapshot` для того же источника.
  - Визуальная проверка на 100/150/200% DPI и в тёмной/светлой теме.
  - Проверка по коду (тот же инвариант, что был в WinUI3 PoC): ни один `tw::*`/WinAPI-вызов не происходит между `Dispatcher::Submit` и выполнением задачи на воркер-потоке.

## Definition of Done

- [ ] `src/app/App.vcxproj` собирается в Release без ошибок/warning на `/W4`, линкуется на `tracewell-core.lib`.
- [ ] Unpackaged `App.exe` запускается без установленного стороннего рантайма (Direct2D/DXGI/D3D11 — часть ОС).
- [ ] Виджет-тулкит (`Widget`, `Button`, `TextBlock`, `ListView`, `Panel`, `Theme`) собран и покрывает потребности экрана Startup.
- [ ] Нажатие «Refresh» асинхронно (пул диспетчера → `WM_APP_DISPATCH`) отображает число записей из `StartupRegistryCollector`, совпадающее с `tracewell-cli snapshot`.
- [ ] Ни одного WinAPI/collector-вызова в UI-потоке (проверено по коду).
- [ ] Sidebar-навигация с активным разделом «Startup» и неактивными заглушками для будущих разделов.
- [ ] Корректная перерисовка при `WM_DPICHANGED` (100/150/200%) и смене темы (`WM_SETTINGCHANGE`).
- [ ] Unit-тесты на `Property<T>` и очередь `Dispatcher` проходят в существующем ctest-наборе.
