# Tracewell

[![CI](https://github.com/denfry/tracewell/actions/workflows/ci.yml/badge.svg)](https://github.com/denfry/tracewell/actions/workflows/ci.yml)

Нативное Windows-приложение (C++) для **честной** диагностики и оптимизации системы. Конкурент CCleaner / Advanced SystemCare / Driver Booster — но без плацебо-фич, запугивания («у вас 47 проблем!») и «универсальных баз драйверов».

## Философия

- **Данные вместо эвристик** — PDH, ETW, SMART через IOCTL, а не догадки.
- **Прозрачность** — каждая цифра в UI имеет объяснение «как измерено» (источник, метод, погрешность).
- **Обратимость** — любое действие журналируется и откатывается; чистка — через карантин.
- **Никакой чистки реестра** — это плацебо, её не будет.
- **Никаких kernel-драйверов** — только документированные user-mode API.

## Модули (по фазам)

| Фаза | Что | Статус |
|------|-----|--------|
| 0 | Core: collectors, Snapshot API, SQLite storage, diff (CLI) | 🚧 в работе |
| 1 | MVP: автозагрузка (Registry/Tasks/Services/Startup) + Disk I/O + UI | планируется |
| 2 | ETW boot trace, SMART (ATA/NVMe), тренды деградации | планируется |
| 3 | Безопасная чистка диска: dry-run, карантин, откат | планируется |
| 4 | Драйверы: Windows Update Catalog / вендорские каналы, restore points | планируется |
| 5 | Explainability-слой, UX, автообновление → 1.0 | планируется |
| 6 | Термальный мониторинг (NVAPI/ADLX), аналитика служб | опционально |

Полная фазовая документация — в [`docs/`](docs/), начиная с [roadmap](docs/00-roadmap.md).

## Сборка

Требования: Visual Studio 2022 (MSVC v143, C++20), CMake ≥ 3.28, [vcpkg](https://github.com/microsoft/vcpkg) (`VCPKG_ROOT` в окружении).

```powershell
cmake --preset default
cmake --build --preset release
ctest --preset release
```

Артефакт: `build/src/cli/Release/tracewell-cli.exe`.

## CLI (Phase 0)

```text
tracewell-cli snapshot          # собрать снапшот всеми collector'ами и сохранить в SQLite
tracewell-cli list              # список сохранённых снапшотов
tracewell-cli show <id>         # показать снапшот (JSON)
tracewell-cli diff <id1> <id2>  # что появилось/исчезло/изменилось между снапшотами
```

БД: `%LOCALAPPDATA%\Tracewell\tracewell.db`.

## Структура

```text
docs/        фазовая документация (Phase 0–6 + roadmap)
src/core/    статическая библиотека: контракты, storage, diff, collectors
src/cli/     консольная утилита (валидация core до появления UI)
tests/       unit-тесты (Catch2)
```

## Лицензия

[MIT](LICENSE)
