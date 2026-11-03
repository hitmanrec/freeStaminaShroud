#!/usr/bin/env python3
"""Record UTC action brackets alongside Enshwick logs; never accesses the game."""
import argparse
import csv
from datetime import datetime, timezone
from pathlib import Path
import re

PHASES = [
    ("IDLE", "15 секунд спокойно стоять без врагов, не атаковать."),
    ("EQUIP", "Без врагов: выбрать оружие, топор, кирку по очереди (по 5 секунд), не атаковать."),
    ("SPRINT", "Без врагов: 10 секунд спринта по земле, затем 15 секунд стоять. Без прыжков/планера."),
    ("JUMP", "Без врагов: 3 обычных прыжка с паузами, затем 10 секунд стоять. Без спринта/двойного прыжка."),
    ("GLIDE", "Если безопасно: 5–10 секунд планера, приземлиться и стоять 10 секунд. Можно пропустить."),
    ("AXE_TREE", "Без врагов: 3–5 ударов топором по дереву, затем 10 секунд стоять."),
    ("PICK_ORE", "Без врагов: 3–5 ударов киркой по руде; запомнить материал, затем 10 секунд стоять."),
    ("PICK_DIRT", "Контроль: 3 удара киркой по обычной земле, затем 10 секунд стоять. Можно пропустить."),
    ("EMPTY_SWINGS", "Без врагов: 2 взмаха топором в воздух, затем 2 киркой. Не задевать объекты."),
    ("AGGRO", "Найти слабого врага, дать ему заметить вас. До 10 секунд не атаковать; по возможности не получать урон."),
    ("LOCK", "На живом враге включить/выключить захват цели (Tab или ваша клавиша). Не атаковать, если возможно."),
    ("COMBAT", "Дать слабому врагу нанести один удар, один раз ударить его топором, затем обычным оружием закончить бой."),
    ("RECOVER", "Когда рядом нет активных врагов: 30 секунд спокойно стоять, затем короткий спринт и один прыжок."),
    ("RELOAD", "Выйти в главное меню, снова открыть тот же мир через Host и постоять 15 секунд."),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / ".build/session-markers.tsv")
    parser.add_argument("--mark", help="Append one manual label instead of the guided session")
    args = parser.parse_args()
    if args.mark and not re.fullmatch(r"[A-Z][A-Z0-9_]{0,63}", args.mark):
        parser.error("Use an uppercase label such as AXE_TREE_BEGIN")
    output = args.output.expanduser()
    output.parent.mkdir(parents=True, exist_ok=True)
    run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    with output.open("a", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, delimiter="\t")
        if stream.tell() == 0:
            writer.writerow(["utc", "run", "phase", "boundary", "note"])

        def mark(phase, boundary, note=""):
            stamp = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
            writer.writerow([stamp, run, phase, boundary, note])
            stream.flush()
            print(f"{stamp} {phase} {boundary}")

        if args.mark:
            mark(args.mark, "MANUAL")
            return
        print("Сначала запустите игру и откройте мир через Host. Этот скрипт только записывает метки.")
        print("Перед этапом Enter → вернитесь в игру → выполните действие → вернитесь сюда и завершите этап.")
        print("Перемещение к месту теста делайте МЕЖДУ этапами. Метки включают время переключения окон.")
        print("В начале этапа s пропускает его, q завершает сессию. Ctrl+C сохраняет уже записанное.")
        try:
            for phase, instruction in PHASES:
                print(f"\n{phase}: {instruction}")
                answer = input("Enter — начать; s — пропустить; q — закончить: ").strip().lower()
                if answer == "q":
                    mark(phase, "STOP")
                    break
                if answer == "s":
                    mark(phase, "SKIP")
                    continue
                mark(phase, "BEGIN")
                note = input("После действия вернитесь сюда. Примечание (или пусто) + Enter — конец: ")
                mark(phase, "END", note)
            else:
                mark("SESSION", "COMPLETE")
        except (KeyboardInterrupt, EOFError):
            mark("SESSION", "INTERRUPTED")
        print(f"Метки сохранены: {output}")


if __name__ == "__main__":
    main()
