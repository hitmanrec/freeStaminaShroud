# freeStaminaShroud

An Enshrouded mod in development: free sprinting and jumping outside combat,
plus free axe tree-cutting and pickaxe ore-mining regardless of combat. Enemy
attacks retain their normal stamina costs.

Version **0.4.3** adds an experimental gameplay patch: free **sprint** outside
observed combat, for the exact client build identified in the supplied reports.
Double jumps, slope jumps and tools still have vanilla costs. Combat detection
uses AI targets and hit events with a 10-second cooldown; this is not a verified
built-in battle flag. The game/Proton and multiplayer behavior need testing.

- [Установка и проверка Sprint Preview 0.4.3 на Arch / Proton](docs/arch-proton.md)
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
