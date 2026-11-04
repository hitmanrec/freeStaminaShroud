# Установка и обновление

[← Главная](../README.md)

Руководство для **Arch и Arch-based Linux**, обычного Steam и Proton.
Проверенная конфигурация — компьютер, на котором игра открывает мир через Host.
Для Flatpak Steam пути и доступ к библиотеке потребуют отдельной настройки.

## 1. Собрать мод

Полностью закройте игру. Установите инструменты и получите исходники:

```sh
sudo pacman -S --needed base-devel zig python git binutils
git clone https://github.com/hitmanrec/freeStaminaShroud.git
cd freeStaminaShroud
sh scripts/test-all.sh
sh scripts/build-probe.sh
```

Проверен **Zig 0.14.1**. Пакет Arch может содержать другую версию. Если нужный
Zig установлен отдельно, укажите его путь:

```sh
ZIG=/полный/путь/к/zig sh scripts/build-probe.sh
```

Результат сборки:

```text
.build/probe/hitmanrec.stamina-probe/
├── stamina_probe.dll
└── SHA256SUMS
```

Это Windows x86-64 DLL: Proton загружает её как Windows-библиотеку.
Windows SDK и Wine для сборки не нужны. SDK Enshwick уже включён в репозиторий;
его контрольная сумма проверяется перед компиляцией.

## 2. Получить загрузчик

```sh
python3 scripts/download-probe-loader.py
```

Скрипт получает официальный Enshwick и проверяет закреплённый SHA-256.
Архив сохраняется в `.build/enshwick.zip`.

Если появилась ошибка `Loader archive changed`, используйте сохранённый
совместимый архив либо дождитесь обновления проверенной пары loader/SDK.
Адрес upstream `dev` изменяемый; отключение проверки хеша не обеспечивает
совместимость. Подробности закреплённой версии — в [руководстве разработчика](development.md#совместимость).

## 3. Установить

Сохраните резервную копию мира. Найдите папку с `enshrouded.exe` через
Steam → Enshrouded → Properties → Installed Files → Browse.
Для стандартной библиотеки:

```sh
GAME_DIR="$HOME/.local/share/Steam/steamapps/common/Enshrouded"
python3 scripts/install-probe.py --game-dir "$GAME_DIR" \
  --loader-zip .build/enshwick.zip --dry-run
python3 scripts/install-probe.py --game-dir "$GAME_DIR" \
  --loader-zip .build/enshwick.zip
```

При другой библиотеке замените путь. `--dry-run` показывает план установки.
Скрипт устанавливает три файла:

```text
Enshrouded/
├── powrprof.dll
└── mods/
    ├── enshwick/enshwick.dll
    └── hitmanrec.stamina-probe/stamina_probe.dll
```

Если подходящий Enshwick уже установлен, параметр `--loader-zip` можно опустить.
Если `powrprof.dll` принадлежит другому загрузчику, сначала проверьте совместимость.

## 4. Настроить Proton

В Steam → Properties → General → Launch Options добавьте:

```text
WINEDLLOVERRIDES="powrprof=n,b" %command%
```

При наличии других launch options объедините параметры, сохранив необходимые
настройки. В меню Mods включите **Free Stamina Shroud - Gameplay Preview**,
затем откройте мир через Host.

Для первого запуска достаточно безопасного спринта, двойного прыжка и взмахов
инструментами. Инструкции для гостей находятся в [руководстве по мультиплееру](multiplayer.md).

## Обновить существующую установку

При полностью закрытой игре, из папки репозитория:

```sh
git pull --ff-only
sh scripts/build-probe.sh
GAME_DIR="$HOME/.local/share/Steam/steamapps/common/Enshrouded"
python3 scripts/install-probe.py --game-dir "$GAME_DIR" --replace
```

`--replace` сохраняет заменяемые файлы в
`Enshrouded/.stamina-probe-backups/<UTC timestamp>/`. Без этого параметра
установщик отказывается перезаписывать отличающиеся файлы. Одинаковая
повторная установка ничего не меняет.

## Windows

Используется та же `stamina_probe.dll` и совместимый Windows-загрузчик Enshwick.
Сборку можно выполнить в Linux, затем перенести DLL. Установку загрузчика на
нативной Windows выполняйте по его [официальной инструкции](https://github.com/enshwick/enshwick).
Отдельный прогон нативной Windows пока не подтверждён.

## Удалить

Закройте игру и перенесите `mods/hitmanrec.stamina-probe` за пределы `mods`.
Enshwick можно оставить для других модов.

Для удаления загрузчика уберите `powrprof.dll`, `mods/enshwick/` и добавленный
override Proton, если они больше не нужны другим модам. При замене прежних
DLL восстановите нужные файлы из резервной копии. Сохранения не удаляйте.
