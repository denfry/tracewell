# Phase 1: MVP диагностики — автозагрузка + Disk I/O + базовый UI

## Цель фазы

Устанавливаемое GUI-приложение, которое:

- показывает **полную картину автозагрузки** (Registry Run-keys, Task Scheduler, Services auto-start, Startup folders) с оценкой влияния каждой записи на старт системы — с явной пометкой «оценка» до появления ETW в Phase 2;
- показывает **live Disk I/O** (per-disk и per-process) на данных PDH, обновление раз в 1–2 сек;
- умеет **отключать/включать** записи автозагрузки обратимо (не удалять!), с журналом действий;
- каждая цифра в UI имеет подсказку «как это измерено».

Измеримо: список автозагрузки совпадает с Sysinternals Autoruns по покрываемым категориям ≥ 95%; отключение записи переживает перезагрузку и корректно откатывается; приложение живёт в фоне < 1% CPU, < 80 MB RAM.

## Границы (Scope)

**Входит:**
- Автозагрузка: 4 источника (Run-keys, Startup folders — из Phase 0; **новые:** Task Scheduler, Services) + enable/disable + журнал отката.
- Оценка задержки старта (файловые метрики + WMI-данные, честно помеченная как приблизительная).
- Disk I/O: PDH per-disk + per-process I/O rates.
- UI-каркас: навигация, экраны Startup и Disk I/O, панель «как измерено».
- UAC-стратегия: приложение non-elevated, отдельные операции — elevated helper.
- Простейший MSIX/MSI-инсталлятор для внутренних сборок.

**НЕ входит:**
- ETW (точная задержка старта — Phase 2), SMART, тренды/график истории.
- Любая чистка, любые действия кроме enable/disable автозагрузки.
- Автообновление, подпись кода (внутренние сборки — self-signed).
- Локализация (строки — через таблицу ресурсов, но язык один).

## Технические задачи

### 1.1 Выбор UI-фреймворка (решить в первые 3 дня)

**Рекомендация: WinUI 3 (Windows App SDK, unpackaged или MSIX) + C++/WinRT.**

- За: современный вид без ручной отрисовки, XAML-биндинги, тёмная тема бесплатно, official path Microsoft.
- Против/риски: тяжелее дистрибуция (Windows App SDK runtime), C++/WinRT-многословность, миграции WASDK между версиями. Митигация: self-contained deployment (+~40 MB, зато нет зависимости от установленного runtime); UI-слой строго отделён от core (core остаётся статической либой из Phase 0, UI — только представление).
- Альтернатива, если WinUI 3 упрётся (fallback-решение зафиксировать ADR'ом): Win32 + Direct2D со своей компонентной прослойкой — дороже по времени на ~2 недели.
- Архитектура: MVVM; core работает в фоновых потоках, UI получает данные через диспетчер (`DispatcherQueue`); ни одного WinAPI-вызова из UI-потока.

### 1.2 Collector: Task Scheduler (`startup.tasks`)

- COM API `ITaskService` (`taskschd.h`): `ITaskService::Connect` → рекурсивный обход папок `ITaskFolder::GetFolders/GetTasks`.
- Отбор задач, влияющих на старт/логон: триггеры `TASK_TRIGGER_BOOT`, `TASK_TRIGGER_LOGON` (+ `TASK_TRIGGER_SESSION_STATE_CHANGE`).
- Для каждой: enabled, автор, действия (`IExecAction` — путь/аргументы), last run result, задержка триггера (`ITrigger::get_Delay`).
- Disable: `IRegisteredTask::put_Enabled(VARIANT_FALSE)` — обратимо и штатно.
- Риск: чтение части задач требует admin (задачи SYSTEM). Non-elevated режим показывает их read-only с пометкой; enable/disable — через elevated helper (1.5).

### 1.3 Collector: Services auto-start (расширение Phase 0)

- К данным Phase 0 добавить: delayed auto-start, триггерный старт (`SERVICE_CONFIG_TRIGGER_INFO`) — триггерные службы **не** считать «замедляющими старт»; зависимости (`QueryServiceConfigW` → `lpDependencies`).
- Disable службы = смена start type на `SERVICE_DEMAND_START` через `ChangeServiceConfigW` (не `DISABLED` — мягче и безопаснее; UI объясняет разницу).
- **Стоп-лист:** редактирование системных служб Microsoft из списка защищённых (антивирус, WinDefend, аудио, сеть) запрещено в UI, а не просто «не рекомендовано». Список — data-driven (JSON-ресурс), обновляемый.

### 1.4 Оценка влияния на старт (без ETW — честная эвристика)

До Phase 2 показываем **Impact score (est.)**, вычисляемый из:

- размер бинаря и число подгружаемых DLL рядом (грубый прокси I/O при старте);
- тип: служба auto (не delayed) > Run-ключ > delayed > logon-задача с delay;
- исторические данные Windows: `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved` (state) и данные Startup apps из `Microsoft-Windows-Diagnostics-Performance` event log (Event ID 100–110: boot degradation, per-app boot time) — это **реальные измерения Windows**, доступные без ETW-трейсинга; парсить через `EvtQuery`/`EvtNext` (`winevt.h`).
- UI-текст обязан говорить: «оценка на основе журнала производительности Windows; точное измерение появится при включённом boot-трейсинге» — прямой задел на Phase 2.

### 1.5 Elevation-стратегия

- Основной процесс — asInvoker. Действия записи в HKLM/SCM/задачи SYSTEM — отдельный `tracewell-elevated.exe`:
  - запуск `ShellExecuteExW` с `runas`, короткоживущий, принимает одну подписанную (HMAC от сессионного ключа) команду через stdin/named pipe, исполняет, пишет результат в журнал, выходит;
  - никакого постоянного elevated-сервиса в MVP (меньше attack surface и меньше вопросов у антивирусов).
- Все мутации — через общий `ActionJournal` (SQLite): `action_id, timestamp, target, before_state, after_state, undo_recipe`. Undo = проигрывание `undo_recipe` (тот же механизм, что и прямое действие).

### 1.6 Disk I/O экран

- Per-disk: PDH-collector из Phase 0, переведённый в streaming-режим (сэмпл раз в 1 сек, кольцевой буфер 10 мин в памяти, в SQLite — минутные агрегаты).
- Per-process: `NtQuerySystemInformation(SystemProcessInformation)` даёт `IO_COUNTERS`-подобные накопительные счётчики per-process; rate = дельта между сэмплами. Альтернатива — `GetProcessIoCounters` по хэндлу (требует `PROCESS_QUERY_LIMITED_INFORMATION`; для чужих/защищённых процессов покажем «н/д», не ноль).
- Внимание: `NtQuerySystemInformation` — полудокументированный; изолировать в один модуль с проверкой версии структуры, graceful fallback на per-disk-only.
- UI: таблица процессов (сортировка по I/O rate), sparkline per-disk, всё с подписью источника данных.

### 1.7 Инсталлятор (внутренний)

- MSIX для WinUI 3-пути (упрощает clean uninstall) или WiX MSI, если unpackaged. Достаточно внутренней раздачи; подпись и SmartScreen — Phase 5.

## Зависимости от предыдущих фаз

Phase 0 целиком: ICollector, Snapshot, SQLite, diff, логирование. Новые collector'ы обязаны жить в том же контракте.

## Риски и митигация

| Риск | Митигация |
|------|-----------|
| WinUI 3 deployment/стабильность | Self-contained; PoC-окно в первые 3 дня фазы; fallback-план Win32+D2D зафиксирован |
| Антивирусные false positives (запись в Run-ключи, elevated helper) | Не паковать/не обфусцировать; консистентные метаданные версий; заранее завести процесс сабмита в Microsoft Defender vendor portal; тест на VirusTotal каждой сборки |
| Отключение автозагрузки ломает пользователю ПО (например, облачный клиент) | Только обратимые операции + журнал + кнопка Undo прямо в тосте после действия; стоп-лист системных записей |
| Per-process I/O для защищённых процессов (PPL) недоступен | Показ «н/д» с объяснением, не нули |
| Impact-оценку примут за точное измерение | Везде бейдж «est.», tooltip с методикой; это же — маркетинговое отличие от конкурентов |
| Diagnostics-Performance журнал пуст/отключён | Фича-детект: если журнала нет — score только из статических факторов, с понижением confidence в UI |

## Критерии готовности (Definition of Done)

- [ ] Полнота автозагрузки ≥ 95% против Autoruns на 5 эталонных машинах (по категориям: Run/RunOnce, Startup folders, Tasks boot/logon, Services auto).
- [ ] Enable/disable для каждой категории: работает, переживает reboot, Undo восстанавливает точное прежнее состояние (побайтово для реестра).
- [ ] Non-admin запуск: всё читается (или помечено «нужны права»), ни одного крэша; elevation запрашивается только по действию.
- [ ] Disk I/O: значения per-disk расходятся с Task Manager/Resource Monitor ≤ 10% на стационарной нагрузке.
- [ ] Фоновое потребление: < 1% CPU (среднее за 10 мин), < 80 MB private bytes.
- [ ] Каждая метрика в UI имеет «как измерено».
- [ ] Инсталлятор ставит/удаляет начисто (проверка остатков в реестре и FS).
- [ ] Прогон полной матрицы ОС из roadmap.

## Тестирование

- **Unit:** парсеры триггеров задач, impact-score (фикстуры событий 100–110), ActionJournal undo-рецепты.
- **Интеграционные:** скрипт создаёт тестовые Run-записи/задачи/службу → приложение их видит → disable → reboot (VM snapshot) → проверка → undo → проверка. Автоматизировать на Hyper-V VM с чекпоинтами.
- **Сравнительное ручное:** Autoruns (полнота), Resource Monitor (I/O), Task Manager Startup tab (impact-ранжирование должно быть непротиворечивым).
- **Матрица железа:** HDD-машина обязательна (на ней PDH-очереди и latency ведут себя иначе, чем на NVMe).
- **UI-smoke:** запуск на 100%/150%/200% DPI, тёмная/светлая тема.

## Оценка трудозатрат

**5–7 недель.** UI-каркас и PoC WinUI 3 ~1.5 нед; Task Scheduler + Services collectors ~1 нед; enable/disable + журнал + elevated helper ~1.5 нед; impact score + event log парсинг ~0.5–1 нед; Disk I/O экран ~1 нед; инсталлятор, стабилизация, тест-матрица ~1 нед.

## Открытые вопросы

- MSIX vs unpackaged: MSIX даёт clean uninstall, но осложняет elevated helper (packaged full-trust). Решить на PoC первой недели.
- Показывать ли UWP/Store-автозагрузку (`StartupTask` пакетов) в MVP — рекомендация: read-only показ, управление в Phase 5.
- Минимальная поддерживаемая ОС: предлагается Win10 22H2+ (WASDK требование) — подтвердить.
