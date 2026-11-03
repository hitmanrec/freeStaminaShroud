# freeStaminaShroud

An Enshrouded mod in development: free sprinting and jumping outside combat,
plus free tool actions regardless of combat or target. Attacks with ordinary
combat weapons retain their normal stamina costs.

Version **0.5.1** adds experimental free ground jumps outside observed combat
and free two-handed tool actions at any time, alongside the tested sprint patch.
The tool rule checks the live item's Tools category and animation; ordinary
combat weapons retain their costs. Combat detection uses the selected AI combat
target and hit events with a 10-second cooldown. New jump/tool behavior still
needs in-game validation; guest prediction is not yet confirmed.

- [Исправление первых прыжков и склона в 0.5.1](docs/session-050-jump-fix.md)
- [Правила и короткий тест прыжков/инструментов 0.5.0](docs/action-preview.md)
- [Установка и проверка Gameplay Preview 0.5.1 на Arch / Proton](docs/arch-proton.md)
- [Поддержка новой сборки игры в 0.4.4](docs/build-205-support.md)
- [Исправление целей ИИ и совместный тест с гостем без мода (0.4.3)](docs/guest-test.md)
- [Исправление отказа по селектору в 0.4.2](docs/session-041-analysis.md)
- [Разбор проверки 0.4.0 и диагностика 0.4.1](docs/session-04-analysis.md)
- [Как устроен патч спринта и какие у него ограничения](docs/sprint-preview.md)
- [Одиночный сценарий и метки действий для диагностики 0.2.x](docs/solo-diagnostics.md)
- [Анализ полученных сессий и исправления 0.2.1](docs/session-02-analysis.md)
- [Точки расхода и офлайн-разбор EXE без новой игровой сессии](docs/cost-paths.md)
- [Результат первого разбора EXE и найденные списания](docs/offline-report-analysis.md)
- [Результат второго отчёта и наблюдение стоимости 0.3.0](docs/cost-trace.md)
- [Разбор реальной сессии 0.3.0: прыжки, склон, инструменты](docs/session-03-analysis.md)
- [Исследование API и существующих модов](docs/research.md)
- [Stamina rules and integration plan](docs/implementation.md)

Run all local checks with `sh scripts/test-all.sh`. Build the Windows x86-64 preview
DLL on Linux with `sh scripts/build-probe.sh` (requires Zig and a native C compiler).
