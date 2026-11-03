# freeStaminaShroud

An Enshrouded mod in development: free sprinting and jumping outside combat,
plus free axe tree-cutting and pickaxe ore-mining regardless of combat. Enemy
attacks retain their normal stamina costs.

The repository includes a portable stamina policy and an Enshwick diagnostic
mod that logs combat-state candidates without changing stamina. The stamina
gameplay mod is not implemented yet; the diagnostic DLL still needs in-game
validation.

- [Сборка и установка диагностики на Arch Linux / Proton](docs/arch-proton.md)
- [Одиночный сценарий и метки действий для диагностики 0.2.x](docs/solo-diagnostics.md)
- [Анализ полученных сессий и исправления 0.2.1](docs/session-02-analysis.md)
- [Точки расхода и офлайн-разбор EXE без новой игровой сессии](docs/cost-paths.md)
- [Результат первого разбора EXE и найденные списания](docs/offline-report-analysis.md)
- [Исследование API и существующих модов](docs/research.md)
- [Stamina rules and integration plan](docs/implementation.md)

Run all local checks with `sh scripts/test-all.sh`. Build the Windows diagnostic
DLL on Linux with `sh scripts/build-probe.sh` (requires Zig and a native C compiler).
