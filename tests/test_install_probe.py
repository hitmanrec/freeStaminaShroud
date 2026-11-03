import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/install-probe.py"
spec = importlib.util.spec_from_file_location("install_probe", SCRIPT)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def dll(marker=0):
    data = bytearray(128)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, 0x8664)
    struct.pack_into("<H", data, 86, 0x2000)
    data[-1] = marker
    return bytes(data)


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "Steam Library/Enshrouded"
        self.game.mkdir(parents=True)
        (self.game / "enshrouded.exe").touch()
        self.probe = self.root / "probe.dll"
        self.probe.write_bytes(dll())
        self.loader = self.root / "loader.zip"
        with zipfile.ZipFile(self.loader, "w") as archive:
            for name in installer.LOADER_FILES:
                archive.writestr(name, dll(1))
            archive.writestr("../unwanted.txt", "must not extract")

    def install(self, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            installer.install(self.game, self.probe, **kwargs)

    def test_install_and_repeat_preserves_unrelated_files(self):
        unrelated = self.game / "my-settings.ini"
        unrelated.write_text("keep")
        self.install(loader_zip=self.loader)
        self.install()  # Existing loader, identical probe: idempotent.
        self.assertEqual((self.game / installer.PROBE).read_bytes(), dll())
        self.assertTrue((self.game / installer.LOADER_FILES[1]).is_file())
        self.assertEqual(unrelated.read_text(), "keep")
        self.assertFalse((self.game.parent / "unwanted.txt").exists())
        self.assertFalse((self.game / ".stamina-probe-backups").exists())

    def test_dry_run_does_not_write(self):
        self.install(loader_zip=self.loader, dry_run=True)
        self.assertFalse((self.game / "mods").exists())
        self.assertFalse((self.game / "powrprof.dll").exists())

    def test_conflict_does_not_partially_install(self):
        (self.game / "powrprof.dll").write_bytes(dll(2))
        with self.assertRaises(ValueError):
            self.install(loader_zip=self.loader)
        self.assertFalse((self.game / "mods").exists())
        self.assertEqual((self.game / "powrprof.dll").read_bytes(), dll(2))

    def test_explicit_replacement_backs_up(self):
        (self.game / "powrprof.dll").write_bytes(dll(2))
        self.install(loader_zip=self.loader, replace=True)
        backups = list((self.game / ".stamina-probe-backups").glob("*/powrprof.dll"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), dll(2))
        self.assertEqual((self.game / "powrprof.dll").read_bytes(), dll(1))

    def test_reject_missing_loader(self):
        with self.assertRaises(ValueError):
            self.install()
        self.assertFalse((self.game / "mods").exists())

    def test_late_write_failure_restores_replaced_files(self):
        previous = self.game / "powrprof.dll"
        previous.write_bytes(dll(2))
        original_replace = Path.replace

        def fail_last(source, target):
            if Path(target).name == "enshwick.dll":
                raise OSError("simulated write failure")
            return original_replace(source, target)

        with mock.patch.object(Path, "replace", fail_last):
            with self.assertRaises(OSError):
                self.install(loader_zip=self.loader, replace=True)
        self.assertEqual(previous.read_bytes(), dll(2))
        self.assertFalse((self.game / installer.PROBE).exists())
        self.assertFalse((self.game / installer.LOADER_FILES[1]).exists())

    def test_reject_symlink_destination(self):
        outside = self.root / "outside"
        outside.mkdir()
        (self.game / "mods").symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.install(loader_zip=self.loader)
        self.assertEqual(list(outside.iterdir()), [])

    def test_reject_invalid_probe(self):
        self.probe.write_bytes(b"not a DLL")
        with self.assertRaises(ValueError):
            self.install(loader_zip=self.loader)

    def test_reject_incomplete_archive(self):
        with zipfile.ZipFile(self.loader, "w") as archive:
            archive.writestr("powrprof.dll", dll())
        with self.assertRaises(ValueError):
            self.install(loader_zip=self.loader)
        self.assertFalse((self.game / "mods").exists())


if __name__ == "__main__":
    unittest.main()
