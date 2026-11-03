import importlib.util
from pathlib import Path
import struct
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/inspect-game.py"
spec = importlib.util.spec_from_file_location("inspect_game", SCRIPT)
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def image():
    data = bytearray(0xA00)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 3)
    struct.pack_into("<H", data, 0x94, 240)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<II", data, optional + 112 + 3 * 8, 0x3000, 12)
    struct.pack_into("<II", data, optional + 112 + 6 * 8, 0x2000, 28)
    for index, (name, rva, raw, flags) in enumerate([
        (b".text", 0x1000, 0x400, 0x60000020),
        (b".rdata", 0x2000, 0x600, 0x40000040),
        (b".pdata", 0x3000, 0x800, 0x40000040),
    ]):
        offset = optional + 240 + index * 40
        struct.pack_into("<8sIIII", data, offset, name, 0x200, rva, 0x200, raw)
        struct.pack_into("<I", data, offset + 36, flags)
    data[0x400] = 0xB8  # mov eax, imm32: actual .text candidate.
    struct.pack_into("<I", data, 0x401, inspector.HASHES["stamina_attribute_root"])
    data[0x405] = 0xC3
    struct.pack_into("<I", data, 0x680, inspector.HASHES["stamina_attribute_root"])
    data[0x6A0:0x6B2] = b"stamina_depletion\0" + b"\0"
    struct.pack_into("<III", data, 0x800, 0x1000, 0x1010, 0x2100)
    struct.pack_into("<IIII", data, 0x60C, 2, 24, 0x2040, 0x640)
    data[0x640:0x644] = b"RSDS"
    data[0x644:0x654] = bytes.fromhex(inspector.KNOWN_GUID)
    struct.pack_into("<I", data, 0x654, 520)
    data[0x700] = 1  # UNWIND_INFO version 1, no chain.
    return data


class InspectTests(unittest.TestCase):
    def test_code_matches_exclude_data_and_recover_function(self):
        data = image()
        before = bytes(data)
        report = inspector.analyze(data)
        self.assertEqual(bytes(data), before)
        self.assertTrue(report["fingerprint_matches_probe_logs"])
        self.assertEqual(len(report["hash_matches"]), 1)
        self.assertEqual(report["hash_matches"][0]["rva"], 0x1001)
        self.assertEqual(report["hash_matches"][0]["function"], [0x1000, 0x1010])
        self.assertIn("stamina_depletion", [x["name"] for x in report["candidate_names"]])

    def test_no_function_metadata_does_not_invent_boundary(self):
        data = image()
        struct.pack_into("<II", data, 0x98 + 112 + 3 * 8, 0, 0)
        self.assertIsNone(inspector.analyze(data)["hash_matches"][0]["function"])

    def test_chained_range_includes_real_parent(self):
        data = image()
        # Hash-bearing range 0x1000 is chained to a parent at 0x1020.
        data[0x700] = 1 | (4 << 3)
        struct.pack_into("<III", data, 0x704, 0x1020, 0x1030, 0x2120)
        data[0x720] = 1
        # A sibling continuation has no stamina hash of its own, but belongs
        # to the same parent's control flow and must be exported too.
        struct.pack_into("<I", data, 0x98 + 112 + 3 * 8 + 4, 24)
        struct.pack_into("<III", data, 0x80C, 0x1040, 0x1050, 0x2140)
        data[0x740] = 1 | (4 << 3)
        struct.pack_into("<III", data, 0x744, 0x1020, 0x1030, 0x2120)
        report = inspector.analyze(data)
        inspector.expand_ranges(data, report, [0x1005, 0xDEADBEEF])
        self.assertIn([0x1020, 0x1030], report["disassembly_ranges"])
        self.assertIn([0x1040, 0x1050], report["disassembly_ranges"])
        self.assertEqual(report["sibling_ranges_included"], 1)
        self.assertEqual(len(report["unwind_chains"][0]["chain"]), 2)
        self.assertEqual(report["focus_requests"][1]["status"], "no_runtime_range")

    def test_chained_unwind_cycle_is_reported(self):
        data = image()
        data[0x700] = 1 | (4 << 3)
        struct.pack_into("<III", data, 0x704, 0x1000, 0x1010, 0x2100)
        report = inspector.analyze(data)
        inspector.expand_ranges(data, report, [])
        self.assertIn("Cyclic", report["unwind_chains"][0]["error"])

    def test_unmatched_fingerprint(self):
        data = image()
        struct.pack_into("<I", data, 0x654, 999)
        self.assertFalse(inspector.analyze(data)["fingerprint_matches_probe_logs"])

    def test_reject_truncated_file(self):
        with self.assertRaises(ValueError):
            inspector.analyze(image()[:0x500])

    def test_reject_outside_debug_payload(self):
        data = image()
        struct.pack_into("<I", data, 0x618, 0xFFFFFFF0)
        with self.assertRaises(ValueError):
            inspector.analyze(data)

    def test_reject_wrong_architecture(self):
        data = image()
        struct.pack_into("<H", data, 0x84, 0x14C)
        with self.assertRaises(ValueError):
            inspector.analyze(data)

    def test_reject_malformed_exception_records(self):
        data = image()
        struct.pack_into("<I", data, 0x98 + 112 + 3 * 8 + 4, 13)
        with self.assertRaises(ValueError):
            inspector.analyze(data)


if __name__ == "__main__":
    unittest.main()
