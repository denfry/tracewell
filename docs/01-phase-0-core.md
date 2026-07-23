# Phase 0: Прототипирование core-архитектуры

## Цель фазы

Работающий консольный прототип (`tracewell-cli.exe`), который:

- собирает данные минимум тремя collector'ами (Registry-автозагрузка, Services, PDH disk counters) через **единый интерфейс `ICollector`**;
- складывает результат каждого прогона в **Snapshot** — версионированную, типизированную структуру;
- персистит снапшоты в **SQLite** (embedded, WAL-режим) и умеет читать их обратно;
- выводит diff двух снапшотов в консоль (что появилось/исчезло/изменилось).

Измеримый результат: `tracewell-cli snapshot` за < 5 сек на эталонной машине создаёт снапшот; `tracewell-cli diff <id1> <id2>` печатает корректный diff; всё покрыто unit-тестами, CI собирает x64 Release без warning'ов уровня W4.

## Границы (Scope)

**Входит:**
- Скелет репозитория, система сборки, CI, соглашения по коду.
- Контракты: `ICollector`, `Snapshot`, `StorageLayer`, модель ошибок.
- Три референсных collector'а (без глубины — глубина в Phase 1–2).
- SQLite storage layer со схемой и миграциями.
- Консольный вывод (табличный + JSON) для валидации.
- Логирование и телеметрия ошибок (локальная, в файл).

**НЕ входит:**
- Любой UI.
- ETW, SMART, драйверы, чистка — только заглушки-интерфейсы, если нужны для проверки абстракции.
- Elevation/UAC-механика (прототип запускается и без admin, деградируя функционально).
- Инсталлятор, подпись кода.

## Технические задачи

### 0.1 Инфраструктура проекта

- **Toolchain:** MSVC v143+ (VS 2022), C++20, CMake ≥ 3.28 (presets), vcpkg manifest-mode для зависимостей (`sqlite3`, `nlohmann-json` или `glaze`, `wil`, `catch2` или `gtest`).
- **WIL (Windows Implementation Library)** — обязательна: RAII-обёртки для HANDLE/HKEY, `RETURN_IF_FAILED`, unique_* типы. Резко сокращает утечки и boilerplate.
- CI: GitHub Actions, windows-latest, матрица Debug/Release x64. Статанализ: `/analyze` + clang-tidy.
- Соглашения: `wchar_t`/UTF-16 внутри Win32-границы, UTF-8 в моделях данных и SQLite; вся конвертация — в одном модуле (`MultiByteToWideChar`/`WideCharToMultiByte`).

### 0.2 Контракт ICollector

```cpp
struct CollectorResult {
    std::string collector_id;      // "startup.registry", "services", "disk.pdh"
    int schema_version;            // версия схемы данных этого collector'а
    json payload;                  // типизированные данные
    std::vector<CollectorError> errors;   // partial failure — норма
    std::chrono::milliseconds duration;
};

struct ICollector {
    virtual std::string_view id() const = 0;
    virtual CollectorCaps caps() const = 0;   // needs_admin, is_streaming, cost_class
    virtual CollectorResult collect(CancellationToken) = 0;
    virtual ~ICollector() = default;
};
```

Ключевые решения, которые надо зафиксировать именно здесь:

- **Partial failure — first-class.** Отказ одного ключа реестра не валит collector; отказ одного collector'а не валит снапшот. Каждая ошибка несёт `HRESULT`/`GetLastError`, контекст (путь/счётчик) и severity.
- **CancellationToken** сквозной — в Phase 1 UI должен уметь отменять сбор.
- **cost_class** (`fast`/`slow`/`streaming`) — планировщик снапшотов в Phase 2 будет собирать быстрые чаще.
- Streaming-collector'ы (PDH, позже ETW) реализуют `collect()` как «накопить N секунд и вернуть агрегат»; отдельный push-интерфейс отложен до Phase 2 — не проектировать заранее.

### 0.3 Референсные collector'ы

1. **`startup.registry`** — WinAPI: `RegOpenKeyExW`/`RegEnumValueW` (обязательно оба view: `KEY_WOW64_64KEY` и `KEY_WOW64_32KEY`) по путям:
   - `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`, `RunOnce`
   - `HKCU\...\Run`, `RunOnce`
   - Startup-папки: `FOLDERID_Startup`, `FOLDERID_CommonStartup` через `SHGetKnownFolderPath`.
   - Для каждой записи: раскрытие `REG_EXPAND_SZ` (`ExpandEnvironmentStringsW`), извлечение пути exe из командной строки (`CommandLineToArgvW` + fallback-эвристика на неквотированные пути), атрибуты файла, версия (`GetFileVersionInfoW`), подпись (`WinVerifyTrust` — только флаг signed/unsigned, без ревокации: она медленная и требует сети).
2. **`services`** — `OpenSCManagerW` + `EnumServicesStatusExW` (`SC_ENUM_PROCESS_INFO`): имя, display name, тип запуска (включая delayed auto-start через `QueryServiceConfig2W`/`SERVICE_CONFIG_DELAYED_AUTO_START_INFO`), состояние, PID, путь бинаря, аккаунт.
3. **`disk.pdh`** — PDH: `PdhOpenQueryW`, счётчики `\PhysicalDisk(*)\Disk Reads/sec`, `Disk Writes/sec`, `Avg. Disk sec/Transfer`, `Current Disk Queue Length`, `% Idle Time`. Два `PdhCollectQueryData` с интервалом 1 сек (rate-счётчики требуют двух сэмплов). Локализация имён счётчиков — **риск №1** (см. ниже): использовать индексы из `PdhLookupPerfNameByIndexW` или английские имена через `PdhAddEnglishCounterW`.

### 0.4 Snapshot API

- `Snapshot = { id (ULID), created_at (UTC), machine_fingerprint, os_build, collector_results[] }`.
- `machine_fingerprint` — хэш от MachineGuid (`HKLM\SOFTWARE\Microsoft\Cryptography`) — чтобы БД, скопированная на другую машину, не смешивала тренды.
- Diff-движок: ключ сущности определяет collector (для автозагрузки — нормализованный путь+имя записи), diff = added/removed/changed по полям. Diff — чистая функция над двумя JSON payload'ами, тестируется без Windows API.

### 0.5 SQLite storage layer

- SQLite amalgamation через vcpkg, компиляция с `SQLITE_ENABLE_JSON1`, WAL, `busy_timeout`.
- Схема v1:
  - `snapshots(id TEXT PK, created_at INTEGER, machine TEXT, os_build TEXT)`
  - `collector_results(snapshot_id, collector_id, schema_version, payload JSON, duration_ms)`
  - `errors(snapshot_id, collector_id, hresult INTEGER, context TEXT, severity INTEGER)`
  - `meta(key, value)` — хранит `schema_version` БД.
- Миграции: линейные, номерные, применяются на старте в транзакции. Написать миграцию v1→v2 (фиктивную) уже сейчас — чтобы механизм был проверен до того, как появятся реальные данные пользователей.
- Расположение: `%LOCALAPPDATA%\Tracewell\tracewell.db`.

### 0.6 Логирование

- Свой тонкий логгер или spdlog: ротация файла в `%LOCALAPPDATA%\Tracewell\logs`, уровни, включение verbose через ключ CLI. Каждая WinAPI-ошибка логируется с `HRESULT` и сообщением `FormatMessageW`.

## Зависимости от предыдущих фаз

Нет. Phase 0 — фундамент; все последующие фазы зависят от её контрактов.

## Риски и митигация

| Риск | Митигация |
|------|-----------|
| Локализованные имена PDH-счётчиков (немецкая/русская Windows) | Только `PdhAddEnglishCounterW` (Vista+); тест на не-английской locale VM |
| Неверная абстракция ICollector — вскроется в Phase 2 на ETW | До закрытия фазы написать «бумажный» прототип ETW-collector'а поверх контракта (не работающий, но компилирующийся) — проверка, что интерфейс не ломается |
| 32/64-bit registry redirection — пропуск половины автозагрузки | Явные тесты на оба WOW64-view; сборка только x64 |
| SQLite corruption при kill процесса | WAL + транзакции на снапшот целиком; тест: TerminateProcess во время записи, БД должна открыться |
| JSON payload превращается в бестиповую кашу | Схема каждого collector'а описана структурой C++ + (де)сериализация; `schema_version` обязателен с v1 |

## Критерии готовности (Definition of Done)

- [ ] `tracewell-cli snapshot` создаёт снапшот с 3 collector'ами; работает под обычным пользователем (ошибки доступа — как partial errors, не крэш).
- [ ] `tracewell-cli list`, `show <id>`, `diff <id1> <id2>`, `export --json` работают.
- [ ] Diff корректен на синтетических фикстурах (unit-тесты) и на реальной машине (добавил запись в Run → diff её видит).
- [ ] Unit-тесты: diff-движок, storage (in-memory SQLite), парсинг командных строк автозагрузки, конвертация UTF-8/16. Покрытие логики без WinAPI ≥ 80%.
- [ ] CI зелёный: сборка, тесты, /analyze без новых предупреждений.
- [ ] Прогон на Win10 22H2 и Win11 24H2, включая одну систему с не-английской locale.
- [ ] Записан ADR-документ (architecture decision record) по каждому решению из 0.2/0.4/0.5.

## Тестирование

- **Unit:** вся не-WinAPI логика; WinAPI-обёртки — через тонкие интерфейсы с фейками.
- **Интеграционные (на живой машине):** снапшот → запись в Run-ключ тестовой утилитой → снапшот → diff. Скриптуется PowerShell'ом.
- **Ручное:** прогон CLI на матрице из roadmap; проверка вывода глазами против Autoruns (Sysinternals) — эталон полноты автозагрузки.
- **Отказоустойчивость:** запуск без прав на часть ключей HKLM (non-admin), отключение диска со счётчиками, kill во время записи БД.

## Оценка трудозатрат

**3–4 недели.** Инфраструктура+CI ~4 дня; контракты и ADR ~3 дня; collector'ы ~5 дней; storage+миграции ~3 дня; diff ~2 дня; тесты и стабилизация ~4 дня.

## Открытые вопросы (решить до конца фазы)

- JSON-библиотека: nlohmann (удобно) vs glaze (быстро, compile-time схемы). Рекомендация: nlohmann на прототип, решение зафиксировать ADR'ом.
- ULID vs UUIDv7 для id снапшотов (важно для сортировки в SQLite) — рекомендация ULID.
- Формат `machine_fingerprint` при переустановке Windows (MachineGuid меняется) — принять как «новая машина», задокументировать.
