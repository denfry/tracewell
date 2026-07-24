# ADR 0001: WinUI 3 PoC — откат на Win32 + Direct2D

Дата: 2026-07-24
Статус: Принято

## Контекст

`docs/02-phase-1-mvp.md` (раздел 1.1) требовал снять риск «WinUI 3 deployment/стабильность» PoC-окном в первые дни Phase 1, с явно зафиксированным fallback-планом (Win32 + Direct2D), если WinUI 3 «упрётся». Дизайн и план PoC — `docs/superpowers/specs/2026-07-23-winui3-poc-design.md` и `docs/superpowers/plans/2026-07-23-winui3-poc.md`.

## Что произошло

Скелет (`src/app/App.vcxproj` и сопутствующие файлы, Task 1 плана) был написан по спецификации и совпадает с официальным шаблоном unpackaged self-contained WinUI 3 C++/WinRT приложения (`ApplicationType=Windows Store`, `WindowsPackageType=None`, `WindowsAppSDKSelfContained=true`).

Сборка через командную строку (`msbuild App.vcxproj`) на этой машине упёрлась в цепочку проблем окружения:

1. Оба установленных инстанса MSBuild (VS Community 2022, VS BuildTools 2022) не содержали C++ UWP/Windows Store build tools для x64 — `OutputPath` не резолвился для `Configuration=Release|Platform=x64`. Исправлено установкой компонента `Microsoft.VisualStudio.Component.VC.Tools.UWP.Store`, затем полного workload `Microsoft.VisualStudio.Workload.UniversalBuildTools` (первый добавил только ARM64/ARM64EC/Base UWP MSBuild-пакеты, без папки `Platforms/x64`).
2. После этого `OutputPath` резолвился, но NuGet-restore падал с ошибкой `Your project does not reference "UAP,Version=v10.0" framework`.

Диагностика (`/v:diag` лог, `restore-diag.log`) показала: MSBuild-таск `NuGet.Build.Tasks.GetProjectTargetFrameworksTask` (NuGet.Frameworks 6.14.1.1, встроенный в BuildTools) получает корректные входные параметры —
`TargetPlatformIdentifier=UAP`, `TargetPlatformVersion=10.0.26100.0`, `TargetPlatformMinVersion=10.0.17763.0` — но всё равно возвращает `_RestoreProjectFramework=native,Version=v0.0`. Явное добавление этих трёх свойств в `App.vcxproj` (Globals PropertyGroup) не изменило результат: значения доходят до таска правильными, сам таск решает неверно.

Обновление VS BuildTools до последнего сервисного релиза (`setup.exe update`) не исправило поведение — баг воспроизводится и на обновлённой версии. Дальнейшая слепая правка NuGet/MSBuild-плампинга сочтена нецелесообразной (уже два раунда установки компонентов + один update).

## Решение

Зафиксировать это как отрицательный результат PoC согласно контингентному плану спеки: WinUI 3 деплой на этой машине через командную строку MSBuild нестабилен/непрактичен для дальнейшей разработки без GUI Visual Studio. Откатываемся на fallback из `docs/02-phase-1-mvp.md` §1.1: **Win32 + Direct2D** со своей компонентной прослойкой.

`src/app/` (WinUI 3 скелет) сохраняется в git как артефакт исследования — код собирается по спецификации, но восстановление пакетов NuGet не проходит в текущем окружении. Не собран, не запущен, не проверены Definition of Done пункты PoC.

## Последствия

- Phase 1 UI-каркас (`docs/02-phase-1-mvp.md` §1.1) строится на Win32 + Direct2D. Эта работа — отдельный подпроект, требует нового design/plan цикла.
- Оценка времени Phase 1 (`docs/02-phase-1-mvp.md`, "~5–7 недель") нуждается в пересмотре: fallback указан там как "дороже по времени на ~2 недели".
- Если позже появится доступ к полноценной среде с GUI Visual Studio (не только BuildTools через командную строку), можно вернуться и попробовать восстановить пакеты через IDE — это не проверялось (агент не имеет доступа к управлению GUI Visual Studio).
- MSIX vs unpackaged вопрос (`docs/02-phase-1-mvp.md`, "Открытые вопросы") снимается сам собой для этого фреймворка — Win32 + Direct2D не использует Windows App SDK/MSIX-специфичную инфраструктуру.
