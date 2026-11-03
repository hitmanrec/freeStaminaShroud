#!/usr/bin/env python3
"""Install only the probe and, optionally, the two known Enshwick loader DLLs."""
import argparse
from datetime import datetime, timezone
import hashlib
from pathlib import Path
import shutil
import struct
import tempfile
import zipfile

REPO = Path(__file__).resolve().parent.parent
PROBE = Path("mods/hitmanrec.stamina-probe/stamina_probe.dll")
LOADER_FILES = ("powrprof.dll", "mods/enshwick/enshwick.dll")


def validate_dll(data):
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("Expected a Windows x86-64 DLL")
    offset = struct.unpack_from("<I", data, 60)[0]
    if offset + 24 > len(data) or data[offset:offset + 4] != b"PE\0\0":
        raise ValueError("Invalid PE header")
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    flags = struct.unpack_from("<H", data, offset + 22)[0]
    if machine != 0x8664 or not flags & 0x2000:
        raise ValueError("Expected an x86-64 DLL")


def destination(root, relative):
    path = root / relative
    # A Steam library's root itself may be a symlink (resolved by caller), but
    # never follow a mod/loader destination symlink out to unrelated files.
    for part in (path, *path.parents):
        if part == root:
            break
        if part.is_symlink():
            raise ValueError(f"Refusing symlink destination: {part}")
    if path.exists() and not path.is_file():
        raise ValueError(f"Destination is not a regular file: {path}")
    return path


def install(game_dir, probe, loader_zip=None, replace=False, dry_run=False):
    root = Path(game_dir).expanduser().resolve(strict=True)
    if not (root / "enshrouded.exe").is_file():
        raise ValueError("Choose the game directory containing enshrouded.exe")
    data = Path(probe).read_bytes()
    validate_dll(data)
    plan = {str(PROBE): data}
    if loader_zip:
        with zipfile.ZipFile(loader_zip) as archive:
            for name in LOADER_FILES:
                entries = [entry for entry in archive.infolist() if entry.filename == name]
                if len(entries) != 1 or entries[0].file_size > 100 * 1024 * 1024:
                    raise ValueError(f"Missing, duplicate, or oversized loader file: {name}")
                payload = archive.read(entries[0])
                validate_dll(payload)
                plan[name] = payload
    else:
        for name in LOADER_FILES:
            target = destination(root, name)
            if not target.is_file():
                raise ValueError("Enshwick is missing; supply --loader-zip or install it first")
            validate_dll(target.read_bytes())

    changes = []
    for name, payload in plan.items():
        target = destination(root, name)
        if target.exists() and target.read_bytes() == payload:
            print(f"Unchanged: {name}")
            continue
        if target.exists() and not replace:
            raise ValueError(f"Existing different file: {name}; use --replace to back it up")
        changes.append((name, target, payload))
    for name, _, payload in changes:
        print(f"{'Would install' if dry_run else 'Install'}: {name} sha256={hashlib.sha256(payload).hexdigest()}")
    if dry_run or not changes:
        return

    backup = root / ".stamina-probe-backups" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    # Validate the backup root too before writing through it.
    destination(root, str(backup.relative_to(root) / "sentinel"))
    saved = {}
    completed = []
    try:
        # Preserve every replaced file before changing any target.
        for name, target, _ in changes:
            if target.exists():
                copy = backup / name
                copy.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, copy)
                saved[name] = copy
        for name, target, payload in changes:
            target.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as stream:
                temporary = Path(stream.name)
                try:
                    stream.write(payload)
                    stream.flush()
                except BaseException:
                    temporary.unlink(missing_ok=True)
                    raise
            try:
                temporary.replace(target)
            finally:
                temporary.unlink(missing_ok=True)
            completed.append((name, target))
    except BaseException:
        for name, target in reversed(completed):
            if name in saved:
                shutil.copy2(saved[name], target)
            else:
                target.unlink(missing_ok=True)
        raise
    if saved:
        print(f"Previous files backed up: {backup}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True)
    parser.add_argument("--probe", type=Path, default=REPO / ".build/probe/hitmanrec.stamina-probe/stamina_probe.dll")
    parser.add_argument("--loader-zip", type=Path)
    parser.add_argument("--replace", action="store_true", help="Back up different existing files before replacement")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        install(args.game_dir, args.probe, args.loader_zip, args.replace, args.dry_run)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Installation failed: {error}\n")
    print('Steam launch option: WINEDLLOVERRIDES="powrprof=n,b" %command%')


if __name__ == "__main__":
    main()
