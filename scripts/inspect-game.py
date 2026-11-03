#!/usr/bin/env python3
"""Read-only PE analysis: identify stamina-related candidates, not verified hooks."""
import argparse
from bisect import bisect_right
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

# Attribute root from Enshwick; component hashes from the researched reflection
# SDK. A byte match is only a candidate, not evidence of a write or action.
HASHES = {
    "stamina_attribute_root": 0x04B6AA8B,
    "stamina_depletion_component": 0x9358BD6C,
    "stamina_depletion_mod_component": 0x920C72D0,
}
KNOWN_GUID = "69f0e9573b32c24a8838d3fff550824c"


class PE:
    def __init__(self, data):
        self.data = data
        if self.take(0, 2) != b"MZ":
            raise ValueError("Not a PE executable")
        pe = self.unpack("<I", 60)[0]
        if self.take(pe, 4) != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        machine, count = self.unpack("<HH", pe + 4)
        optional_size = self.unpack("<H", pe + 20)[0]
        optional = pe + 24
        self.take(optional, optional_size)
        if machine != 0x8664 or self.unpack("<H", optional)[0] != 0x20B:
            raise ValueError("Expected an x86-64 PE32+ executable")
        if optional_size < 112 or not 1 <= count <= 96:
            raise ValueError("Unsupported PE header")
        self.base = self.unpack("<Q", optional + 24)[0]
        directory_count = self.unpack("<I", optional + 108)[0]
        if directory_count > (optional_size - 112) // 8:
            raise ValueError("Truncated PE directory table")
        self.directories = [self.unpack("<II", optional + 112 + i * 8)
                            for i in range(directory_count)]
        self.sections = []
        for i in range(count):
            header = optional + optional_size + i * 40
            raw_name, virtual_size, rva, raw_size, raw_offset = self.unpack("<8sIIII", header)
            flags = self.unpack("<I", header + 36)[0]
            self.take(raw_offset, raw_size)
            self.sections.append({
                "name": raw_name.rstrip(b"\0").decode("ascii", errors="replace"),
                "rva": rva, "virtual_size": virtual_size,
                "raw_offset": raw_offset, "raw_size": raw_size,
                "executable": bool(flags & 0x20000000),
            })

    def take(self, offset, size):
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise ValueError("PE range outside the file")
        return self.data[offset:offset + size]

    def unpack(self, fmt, offset):
        return struct.unpack(fmt, self.take(offset, struct.calcsize(fmt)))

    def at_rva(self, rva, size):
        for section in self.sections:
            delta = rva - section["rva"]
            if 0 <= delta <= section["raw_size"] and size <= section["raw_size"] - delta:
                return self.take(section["raw_offset"] + delta, size)
        raise ValueError(f"RVA 0x{rva:x} is not file-backed")

    def directory(self, index):
        return self.directories[index] if index < len(self.directories) else (0, 0)

    def fingerprint(self):
        rva, size = self.directory(6)
        if not size:
            return None
        if size % 28 or size > 28 * 1024:
            raise ValueError("Invalid debug directory")
        records = self.at_rva(rva, size)
        for offset in range(0, size, 28):
            kind, length, _, pointer = struct.unpack_from("<IIII", records, offset + 12)
            if kind != 2 or length < 24:
                continue
            value = self.take(pointer, length)
            if value[:4] == b"RSDS":
                # Exclude the PDB path: it may expose the developer's local path.
                return {"guid_le": value[4:20].hex(), "age": struct.unpack_from("<I", value, 20)[0]}
        return None

    def runtime_records(self):
        rva, size = self.directory(3)
        if not size:
            return []
        if size % 12 or size > 12 * 1_000_000:
            raise ValueError("Invalid exception directory")
        entries = self.at_rva(rva, size)
        ranges = []
        for offset in range(0, size, 12):
            begin, end, unwind = struct.unpack_from("<III", entries, offset)
            if begin >= end:
                continue
            if any(s["executable"] and s["rva"] <= begin < end <= s["rva"] + s["raw_size"]
                   for s in self.sections):
                ranges.append((begin, end, unwind))
        return sorted(set(ranges))

    def functions(self):
        return sorted({(begin, end) for begin, end, _ in self.runtime_records()})

    def unwind_chain(self, record):
        """A runtime range may be a continuation, not a callable entry point."""
        chain = []
        seen = set()
        for _ in range(32):
            if record in seen:
                raise ValueError("Cyclic chained unwind metadata")
            seen.add(record)
            begin, end, unwind = record
            if begin >= end:
                raise ValueError("Invalid chained runtime range")
            header = self.at_rva(unwind, 4)
            version, flags, slots = header[0] & 7, header[0] >> 3, header[2]
            if version not in (1, 2):
                raise ValueError("Unsupported unwind metadata version")
            chain.append({"begin": begin, "end": end, "unwind_rva": unwind,
                          "version": version, "flags": flags})
            if not flags & 4:
                return chain
            if flags & 3:
                raise ValueError("Conflicting chained unwind flags")
            tail = unwind + 4 + 2 * ((slots + 1) & ~1)
            record = struct.unpack("<III", self.at_rva(tail, 12))
        raise ValueError("Chained unwind metadata exceeds depth limit")


def expand_ranges(data, report, focused_rvas):
    pe = PE(data)
    records = pe.runtime_records()
    by_range = {(begin, end): (begin, end, unwind) for begin, end, unwind in records}
    requested = {tuple(item["function"]) for item in report["hash_matches"] if item["function"]}
    focus = []
    for rva in focused_rvas:
        found = [record for record in records if record[0] <= rva < record[1]]
        if not found:
            focus.append({"rva": rva, "status": "no_runtime_range"})
        else:
            record = max(found, key=lambda item: item[0])
            requested.add(record[:2])
            focus.append({"rva": rva, "status": "found", "range": list(record[:2])})
    report["focus_requests"] = focus
    report["unwind_chains"] = []
    expanded = set(requested)
    roots = set()
    for pair in sorted(requested):
        try:
            chain = pe.unwind_chain(by_range[pair])
            for entry in chain:
                begin, end = entry["begin"], entry["end"]
                # Validate that a parent range is physically backed executable
                # code before asking objdump to read it.
                if not any(s["executable"] and s["rva"] <= begin < end <= s["rva"] + s["raw_size"] for s in pe.sections):
                    raise ValueError("Unwind parent is outside executable sections")
            report["unwind_chains"].append({"range": list(pair), "chain": chain})
            roots.add((chain[-1]["begin"], chain[-1]["end"]))
            expanded.update((entry["begin"], entry["end"]) for entry in chain)
        except ValueError as error:
            report["unwind_chains"].append({"range": list(pair), "error": str(error)})
    # Other fragments can branch into/out of the captured range. Include the
    # sibling .pdata ranges which chain back to the same root, not unrelated
    # code from a guessed fixed-size window.
    siblings = set()
    for record in records:
        if record[:2] in expanded:
            continue
        try:
            chain = pe.unwind_chain(record)
        except ValueError:
            continue
        if (chain[-1]["begin"], chain[-1]["end"]) in roots:
            siblings.add(record[:2])
    expanded.update(siblings)
    report["sibling_ranges_included"] = len(siblings)
    report["disassembly_ranges"] = [list(pair) for pair in sorted(expanded)]


def analyze(data):
    pe = PE(data)
    fingerprint = pe.fingerprint()
    functions = pe.functions()
    starts = [begin for begin, _ in functions]
    matches = []
    names = []
    for section in pe.sections:
        raw = pe.take(section["raw_offset"], section["raw_size"])
        if section["executable"]:
            for label, value in HASHES.items():
                needle = struct.pack("<I", value)
                offset = raw.find(needle)
                while offset >= 0:
                    rva = section["rva"] + offset
                    index = bisect_right(starts, rva) - 1
                    function = functions[index] if index >= 0 and rva + 4 <= functions[index][1] else None
                    matches.append({"label": label, "value": f"0x{value:08x}", "rva": rva,
                                    "function": list(function) if function else None})
                    if len(matches) > 10000:
                        raise ValueError("Too many hash matches; refusing unbounded report")
                    offset = raw.find(needle, offset + 1)
        else:
            # Plain reflected names, not resources or complete string dumps.
            for match in re.finditer(rb"(?<![A-Za-z0-9_:])[A-Za-z_][A-Za-z0-9_:]{4,159}\x00", raw):
                name = match[0][:-1].decode("ascii")
                if any(term in name.lower() for term in
                       ("stamina", "depletion", "sprint", "jump", "mining", "combat_targets", "cluster_combat")):
                    names.append({"name": name, "rva": section["rva"] + match.start()})
                    if len(names) > 4096:
                        raise ValueError("Too many candidate names; refusing unbounded report")
    return {
        "schema": 2,
        "purpose": "Unverified static candidates; no hooks, patches or gameplay claims",
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data), "image_base": pe.base, "fingerprint": fingerprint,
        "fingerprint_matches_probe_logs": bool(fingerprint and fingerprint["guid_le"] == KNOWN_GUID and fingerprint["age"] == 520),
        "sections": pe.sections, "runtime_function_count": len(functions),
        "hash_matches": sorted(matches, key=lambda item: item["rva"]),
        "candidate_names": sorted(names, key=lambda item: item["rva"]),
    }


def disassemble(exe, output, report, max_functions):
    tool = shutil.which("objdump")
    if not tool:
        report["disassembly_status"] = "unavailable: install binutils"
        return
    functions = sorted({tuple(pair) for pair in report.get("disassembly_ranges", [])} or
                       {tuple(item["function"]) for item in report["hash_matches"] if item["function"]})
    report["candidate_function_count"] = len(functions)
    report["function_limit"] = max_functions
    report["disassembly"] = []
    for begin, end in functions[:max_functions]:
        stop = min(end, begin + 65536)
        command = [tool, "-d", "-Mintel", f"--start-address={report['image_base'] + begin}",
                   f"--stop-address={report['image_base'] + stop}", str(exe)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        text = result.stdout.replace(str(exe), exe.name)
        # A .pdata boundary is an instruction boundary, but may be a chained
        # continuation. The report includes parents; it is not a hook contract.
        name = f"function-{begin:08x}.txt"
        (output / name).write_text(text[:512000], encoding="utf-8")
        report["disassembly"].append({"file": name, "begin": begin, "end": end,
                                      "exit_code": result.returncode,
                                      "truncated": stop < end or len(text) > 512000})
    report["disassembly_status"] = "complete" if all(x["exit_code"] == 0 for x in report["disassembly"]) else "errors"
    report["functions_omitted"] = max(0, len(functions) - max_functions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=Path(".build/offline-report"))
    parser.add_argument("--max-functions", type=int, default=40)
    parser.add_argument("--focus-rva", type=lambda value: int(value, 0), action="append", default=[],
                        help="Also include this callee/code RVA and its unwind parents; repeatable")
    args = parser.parse_args()
    if not 1 <= args.max_functions <= 512:
        parser.error("--max-functions must be between 1 and 512")
    if len(args.focus_rva) > 32 or any(not 0 <= rva <= 0xFFFFFFFF for rva in args.focus_rva):
        parser.error("At most 32 unsigned 32-bit focus RVAs are supported")
    try:
        exe = args.exe.expanduser().resolve(strict=True)
        if exe.stat().st_size > 512 * 1024 * 1024:
            raise ValueError("Executable exceeds the 512 MiB inspection limit")
        if args.out.exists() and any(args.out.iterdir()):
            raise ValueError("Output directory is not empty; choose another --out")
        data = exe.read_bytes()
        report = analyze(data)
        expand_ranges(data, report, args.focus_rva)
        args.out.mkdir(parents=True, exist_ok=True)
        disassemble(exe, args.out, report, args.max_functions)
        (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Report: {args.out / 'report.json'}")
        print(f"Fingerprint matches probe logs: {report['fingerprint_matches_probe_logs']}")
        print(f"Unverified hash matches: {len(report['hash_matches'])}")
        print("The executable was only read. No game launch or modification performed.")
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Inspection failed: {error}\n")


if __name__ == "__main__":
    main()
