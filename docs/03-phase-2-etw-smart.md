# Phase 2: ETW boot trace + SMART-диагностика + тренды во времени

## Цель фазы

- **Реальное измерение загрузки:** приложение по согласию пользователя включает boot-трейсинг, после следующей перезагрузки показывает фактическую стоимость каждого элемента автозагрузки (CPU-время, disk I/O, время до готовности) — заменяя «est.»-оценки Phase 1 измеренными значениями.
- **SMART-диагностика:** состояние всех физических дисков (ATA/SATA и NVMe) с расшифровкой атрибутов, честным health-статусом и объяснением каждого показателя.
- **Тренды:** графики деградации во времени на накопленных снапшотах (время загрузки, SMART-атрибуты, число элементов автозагрузки, I/O baseline).

Измеримо: после boot-трейса ≥ 90% элементов автозагрузки имеют измеренные (не оценочные) метрики; SMART читается на SATA и NVMe; экран трендов строит графики по ≥ 7 дням истории.

## Границы (Scope)

**Входит:**
- ETW: AutoLogger boot-сессия, парсинг трейса, атрибуция затрат к записям автозагрузки, автоостановка и лимиты размера.
- SMART: ATA passthrough + NVMe health log, пороговые оценки, история атрибутов в снапшотах.
- Планировщик фоновых снапшотов (периодический сбор без запуска UI — лёгкая задача в Task Scheduler).
- Экран трендов + миграция схемы SQLite (первая реальная миграция).

**НЕ входит:**
- Полный xperf-класс анализа (wait analysis, стеки) — только атрибуция к автозагрузке.
- Предсказание отказа диска ML-моделями — только пороги и тренды с честными формулировками.
- USB/RAID-специфичные проходы SMART (best effort, задокументированные ограничения).
- Чистка, драйверы.

## Технические задачи

### 2.1 ETW boot trace — архитектура

- **AutoLogger** (не Global Logger): создание сессии записью в `HKLM\SYSTEM\CurrentControlSet\Control\WMI\Autologger\TracewellBoot` (или `StartTraceW` с `EVENT_TRACE_FILE_MODE_...` + autologger-флаги). Требует admin — операция идёт через elevated helper из Phase 1.
- Провайдеры (kernel flags через `EnableFlags` NT Kernel Logger-сессии либо system provider на Win11):
  - `EVENT_TRACE_FLAG_PROCESS | THREAD | IMAGE_LOAD` — кто и когда стартовал;
  - `EVENT_TRACE_FLAG_DISK_IO | DISK_IO_INIT | FILE_IO_INIT` — I/O атрибуция к процессам;
  - `EVENT_TRACE_FLAG_CSWITCH` — **нет** в v1 (объём); CPU-время брать из Process/Thread accounting событий.
  - user-провайдер `Microsoft-Windows-Winlogon` + `Microsoft-Windows-Shell-Core` (`9E9EAF...`; фаза logon, Explorer init) — маркеры границ фаз загрузки.
- Ограничители: `MaximumFileSize` 256–512 MB, circular off (boot конечен), FlushTimer; **страховка** — RunOnce-задача останавливает сессию через N минут после логона, даже если приложение не запустилось. Сессия одноразовая: после сбора elevated helper удаляет Autologger-ключ.
- Парсинг: `OpenTraceW`/`ProcessTrace` + `EVENT_RECORD`-callback, TDH (`TdhGetEventInformation`) для user-провайдеров; kernel-события (MOF) — по документированным layout'ам. Парсер — отдельный процесс/поток с жёстким лимитом памяти: ETL в сотни МБ не должен ронять UI.
- Атрибуция: сопоставление процессов из трейса записям автозагрузки по (нормализованный путь exe, командная строка, родитель: services.exe / taskeng / explorer). Итог на запись: start offset от начала boot, CPU ms, disk bytes, время до idle. Результат — обычный `CollectorResult` (`boot.etw`), схема версионируется.
- Fallback: если парсинг данной сборки Windows ломается — оставляем «est.» из Phase 1 и логируем; никогда не показываем частично неверные цифры как точные.

### 2.2 SMART — ATA/SATA

- Enumerate физических дисков: `SetupDiGetClassDevsW(GUID_DEVINTERFACE_DISK)` или `\\.\PhysicalDriveN` перебором + `IOCTL_STORAGE_QUERY_PROPERTY (StorageDeviceProperty, StorageAdapterProperty)` — модель, серийник, шина (`BusTypeSata/BusTypeNvme/BusTypeUsb/BusTypeRAID`).
- ATA SMART: `DeviceIoControl(SMART_RCV_DRIVE_DATA)` (`ntdddisk.h`) — команды `SMART_READ_DATA` (атрибуты) и `READ_THRESHOLDS`; либо `IOCTL_ATA_PASS_THROUGH` как более современный путь. Требует admin (`GENERIC_READ|GENERIC_WRITE` к `\\.\PhysicalDriveN`) → чтение SMART идёт при elevated-снапшоте или через helper; UI без admin показывает последний сохранённый результат с датой.
- Парсинг атрибутов: raw 6 байт, вендорские интерпретации различаются. Критические для health: 05 (Reallocated), C5 (Pending), C6 (Uncorrectable), C7 (CRC — кабель!), 09 (POH), C2 (Temp), 0A (Spin Retry). Таблица интерпретаций — data-driven JSON с вендорскими override'ами (как у smartmontools drivedb; лицензионно — писать свою таблицу, а не копировать GPL-базу).
- USB-мосты: часть поддерживает SAT (SCSI-ATA Translation) через `IOCTL_SCSI_PASS_THROUGH` с ATA PASS-THROUGH (opcode A1h/85h) — best effort, при отказе диск помечается «SMART недоступен через USB-мост».

### 2.3 SMART — NVMe

- Win10+: `IOCTL_STORAGE_QUERY_PROPERTY` c `StorageAdapterProtocolSpecificProperty` / `StorageDeviceProtocolSpecificProperty`, `ProtocolTypeNvme`, `NVMeDataTypeLogPage`, log `02h` (SMART / Health Information): composite temp, available spare, percentage used, data units read/written, media errors, unsafe shutdowns. Документированный путь, **не** требует vendor SDK; на Win10 старых сборках доступен под admin, на новых — часть данных без admin.
- Health-модель: три статуса — OK / Warning / Critical, каждый с перечнем причин («percentage used 87%», «available spare ниже порога») и объяснением на человеческом языке. Никаких «здоровье 73%» без расшифровки.

### 2.4 Фоновые снапшоты и тренды

- Планировщик: логон-задача Task Scheduler запускает `tracewell-cli snapshot --background` раз в сутки + событийно после boot-трейса. CLI из Phase 0 переиспользуется как есть.
- Retention: сырые снапшоты 90 дней, дальше — downsample в агрегатные ряды (`trend_series(metric_id, ts, value)`); миграция схемы v1→v2 по механизму Phase 0.
- Экран трендов: время загрузки по трейсам/Event 100, SMART-атрибуты по дискам, count автозагрузки, I/O baseline. График — свой D2D/Win2D-контрол или Win2D-хост в WinUI 3 (без web-view).

## Зависимости от предыдущих фаз

- Phase 0: ICollector/Snapshot/SQLite/миграции (тренды — прямое использование), CLI для фоновых снапшотов.
- Phase 1: elevated helper (Autologger, SMART), экран автозагрузки (замена est.→measured), UI-каркас.

## Риски и митигация

| Риск | Митигация |
|------|-----------|
| Kernel-события меняют layout между сборками Windows | Парсер с проверкой версий структур; интеграционный тест-набор эталонных ETL с разных сборок в репозитории; fallback на est. |
| Autologger остаётся включённым (диск копится) | Жёсткий MaxFileSize + сторожевая RunOnce-остановка + удаление ключа после первого разбора; телеметрия «сессия жива дольше N» |
| SMART passthrough вешает/сбоит на кривых контроллерах | Таймауты на DeviceIoControl (отдельный поток + CancelIoEx), персистентный blacklist проблемных устройств по VID/PID |
| Пользователь испугается «Critical» диска | Формулировки review'ятся: статус всегда с причиной, ссылкой «что делать» и кнопкой экспорта отчёта; не пугать по одному severity-фактору |
| Антивирус реагирует на создание Autologger | Действие только по явной кнопке пользователя с объяснением; подписанные бинари (ускорить из Phase 5, хотя бы OV-сертификат уже здесь) |
| ETL-парсинг тяжёлый (CPU/RAM) | Отдельный low-priority процесс, стриминговый разбор без загрузки файла в память, лимиты |

## Критерии готовности (Definition of Done)

- [ ] Полный цикл: кнопка «Измерить загрузку» → согласие → reboot → авторазбор → измеренные метрики у ≥ 90% записей автозагрузки; Autologger-ключ удалён.
- [ ] Сторожевая остановка сессии срабатывает при «пропаже» приложения (тест: удалить exe до перезагрузки).
- [ ] SMART: корректные данные на SATA SSD, HDD, NVMe; USB-мост — данные или честное «недоступно»; сравнение с CrystalDiskInfo/smartctl без противоречий по критическим атрибутам.
- [ ] Тренды рисуются на 7+ днях реальной истории; миграция v1→v2 проходит на БД, созданной сборкой Phase 1.
- [ ] Фоновый снапшот не будит диск без нужды и укладывается в < 30 сек, < 200 MB peak RAM.
- [ ] Матрица: Win10 22H2 + Win11 23H2/24H2, HDD/SATA/NVMe/USB-кейсы.

## Тестирование

- **Unit:** парсер SMART-атрибутов (фикстуры дампов с реальных дисков), health-модель, downsample трендов, атрибуция процессов к записям (фикстуры событий).
- **Интеграционные:** эталонные ETL-файлы (записать на каждой ОС матрицы, положить в test assets) → парсер выдаёт стабильные ожидаемые агрегаты.
- **Ручное:** boot-трейс на HDD-машине (худший случай по времени), на быстрой NVMe; SMART против CrystalDiskInfo и `smartctl -a`; выдёргивание USB-диска во время чтения SMART.
- **Долгосрочное:** 2 недели фоновых снапшотов на dogfood-машинах до закрытия фазы.

## Оценка трудозатрат

**6–8 недель.** ETW: сессия+сторожа ~1 нед, парсер+атрибуция ~2–2.5 нед; SMART ATA ~1 нед, NVMe ~0.5 нед, health-модель+UI ~0.5 нед; фоновый планировщик+retention+миграция ~0.5 нед; тренды-UI ~1 нед; стабилизация ~1 нед.

## Открытые вопросы

- Использовать ли готовый парсер (krabsetw — MIT, ускорит на ~1 нед) vs свой на TDH. Рекомендация: krabsetw для user-провайдеров, свой MOF-разбор для kernel.
- Хранить ли сырые ETL после разбора (полезно для суппорта, но сотни МБ) — предложение: хранить последний, с кнопкой удаления.
- Порог «Warning» для percentage used NVMe (80%? 90%?) — решить с данными, задокументировать в explainability.
