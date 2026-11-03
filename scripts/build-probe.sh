#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
zig_bin=${ZIG:-zig}
command -v "$zig_bin" >/dev/null 2>&1 || {
    echo "Zig is required (Arch: sudo pacman -S --needed zig)." >&2; exit 1;
}
cd "$repo_dir"
sha256sum -c third_party/enshwick/SHA256SUMS
sh scripts/test-probe.sh
out="$repo_dir/.build/probe/hitmanrec.stamina-probe"
mkdir -p "$out"
"$zig_bin" build-lib -target x86_64-windows-msvc \
    -dynamic -fentry=probe_entry -O ReleaseSmall -fno-emit-implib \
    -I third_party/enshwick -femit-bin="$out/stamina_probe.dll" \
    -cflags -std=c11 -Wall -Wextra -Werror -ffreestanding -- probe/probe.c probe/entry.c
(cd "$out" && sha256sum stamina_probe.dll > SHA256SUMS)
echo "Built: $out/stamina_probe.dll"
