#!/usr/bin/env python3
"""Exercise dependency preparation with isolated SDK/package-manager fixtures."""

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent
PACKAGE = "switch-ffmpeg-7.1-4-any.pkg.tar.zst"


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nxcast-ffmpeg-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.scripts = self.root / "scripts"
        self.scripts.mkdir()
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.sdk = self.root / "sdk"
        self.prefix = self.sdk / "portlibs/switch"
        self.cache = self.root / "cache"
        self.cache.mkdir()
        self.log = self.root / "calls"
        self.version = self.root / "version"
        self.env = dict(os.environ, PATH=f"{self.bin}{os.pathsep}{os.environ['PATH']}",
                        DEVKITPRO=str(self.sdk), PORTLIBS_PREFIX=str(self.prefix),
                        NXCAST_FFMPEG_OUTPUT_DIR=str(self.cache),
                        FIXTURE_ROOT=str(self.root))
        for name in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "NXCAST_FFMPEG_PREPARED",
                     "NXCAST_IN_BUILD", "NXCAST_BOOTSTRAP", "NXCAST_AUTO_INSTALL_FFMPEG"):
            self.env.pop(name, None)
        shutil.copyfile(ROOT / "makefile", self.root / "makefile")
        shutil.copy2(ROOT / "scripts/install_switch_ffmpeg_airplay.sh",
                     self.scripts / "install_switch_ffmpeg_airplay.sh")
        payload = b"isolated test package\n"
        (self.cache / PACKAGE).write_bytes(payload)
        fetch = (ROOT / "scripts/fetch_switch_ffmpeg_airplay.sh").read_text()
        fetch = re.sub(r"(?m)^PACKAGE_SHA256=.*$",
                       "PACKAGE_SHA256=" + hashlib.sha256(payload).hexdigest(), fetch)
        self.executable(self.scripts / "fetch_switch_ffmpeg_airplay.sh", fetch)
        self.executable(self.scripts / "verify_switch_ffmpeg_airplay.sh", """#!/bin/sh
echo verify >> "$FIXTURE_ROOT/calls"
""")
        self.executable(self.bin / "dkp-pacman", """#!/bin/sh
if [ "$1" = -Q ]; then
    [ -f "$FIXTURE_ROOT/version" ] || exit 1
    printf 'switch-ffmpeg %s\n' "$(cat "$FIXTURE_ROOT/version")"
    exit 0
fi
echo install >> "$FIXTURE_ROOT/calls"
[ ! -f "$FIXTURE_ROOT/fail-install" ] || exit 1
[ ! -f "$FIXTURE_ROOT/incomplete-install" ] || exit 0
printf '7.1-4\n' > "$FIXTURE_ROOT/version"
mkdir -p "$PORTLIBS_PREFIX/lib" "$PORTLIBS_PREFIX/include/libavutil"
for lib in libavcodec.a libavformat.a libavutil.a; do
    echo archive > "$PORTLIBS_PREFIX/lib/$lib"
done
echo header > "$PORTLIBS_PREFIX/include/libavutil/hwcontext_nvtegra.h"
""")
        self.executable(self.bin / "sudo", """#!/bin/sh
echo sudo >> "$FIXTURE_ROOT/calls"
exec "$@"
""")
        self.executable(self.bin / "curl", """#!/bin/sh
echo download >> "$FIXTURE_ROOT/calls"
exit 1
""")
        self.executable(self.bin / "build-make", """#!/bin/sh
[ "$(cat "$FIXTURE_ROOT/version")" = 7.1-4 ] || exit 20
echo "build $*" >> "$FIXTURE_ROOT/calls"
""")

    def executable(self, path, content):
        path.write_text(content)
        path.chmod(0o755)

    def installed(self, version="7.1-4"):
        self.version.write_text(version + "\n")
        (self.prefix / "lib").mkdir(parents=True)
        for name in ("libavcodec.a", "libavformat.a", "libavutil.a"):
            (self.prefix / "lib" / name).write_text("archive\n")
        header = self.prefix / "include/libavutil/hwcontext_nvtegra.h"
        header.parent.mkdir(parents=True)
        header.write_text("header\n")

    def run_command(self, *args, success=True):
        result = subprocess.run(args, cwd=self.root, env=self.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result.stdout

    def calls(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_current_package_is_offline_noop(self):
        self.installed()
        shutil.rmtree(self.cache)
        output = self.run_command("bash", "scripts/install_switch_ffmpeg_airplay.sh")
        self.assertIn("already installed", output)
        self.assertEqual(self.calls(), [])

    def test_old_or_missing_package_installs_from_verified_cache(self):
        for version in (None, "7.1-3"):
            with self.subTest(version=version):
                if version:
                    self.version.write_text(version + "\n")
                self.run_command("bash", "scripts/install_switch_ffmpeg_airplay.sh")
                self.assertEqual(self.version.read_text().strip(), "7.1-4")
        self.assertEqual(self.calls().count("install"), 2)
        self.assertEqual(self.calls().count("verify"), 2)
        self.assertNotIn("download", self.calls())

    def test_missing_archive_is_repaired(self):
        self.installed()
        (self.prefix / "lib/libavcodec.a").unlink()
        self.run_command("bash", "scripts/install_switch_ffmpeg_airplay.sh")
        self.assertIn("install", self.calls())
        self.assertTrue((self.prefix / "lib/libavcodec.a").is_file())

    def test_bad_cache_and_failed_download_prevent_install(self):
        (self.cache / PACKAGE).write_bytes(b"corrupt\n")
        self.run_command("bash", "scripts/install_switch_ffmpeg_airplay.sh", success=False)
        self.assertEqual(self.calls(), ["download"])

    def test_install_failure_stops_before_build(self):
        (self.root / "fail-install").touch()
        self.run_command("make", "dev-build", f"MAKE={self.bin / 'build-make'}", success=False)
        self.assertIn("install", self.calls())
        self.assertFalse(any(line.startswith("build") for line in self.calls()))
        self.assertNotIn("verify", self.calls())

    def test_success_exit_without_package_is_rejected(self):
        (self.root / "incomplete-install").touch()
        output = self.run_command("bash", "scripts/install_switch_ffmpeg_airplay.sh", success=False)
        self.assertIn("did not provide", output)

    def test_custom_prefix_does_not_modify_global_sdk(self):
        self.env["PORTLIBS_PREFIX"] = str(self.root / "custom")
        output = self.run_command("bash", "scripts/install_switch_ffmpeg_airplay.sh", success=False)
        self.assertIn("NXCAST_AUTO_INSTALL_FFMPEG=0", output)
        self.assertEqual(self.calls(), [])

    def test_each_build_entry_prepares_before_recursive_make(self):
        goals = (None, "all", "dev-build", "dev-rebuild", "release-build",
                 "full-trace-build", "full-trace-rebuild",
                 "playback-baseline-trace-build", "playback-baseline-trace-rebuild")
        for goal in goals:
            with self.subTest(goal=goal):
                self.version.write_text("7.1-3\n")
                if self.log.exists():
                    self.log.unlink()
                args = ["make", "-j4", f"MAKE={self.bin / 'build-make'}"]
                if goal:
                    args.append(goal)
                self.run_command(*args)
                calls = self.calls()
                self.assertEqual(calls.count("install"), 1)
                self.assertEqual(calls.count("verify"), 1)
                self.assertTrue(calls[-1].startswith("build NXCAST_FFMPEG_PREPARED=1"))

    def test_nonbuild_and_optout_do_not_prepare(self):
        for args in (("clean",), ("test-ui",), ("dev-build", "NXCAST_AUTO_INSTALL_FFMPEG=0")):
            with self.subTest(args=args):
                # Deliberately absent SDK rules stop parsing after bypassing bootstrap.
                self.run_command("make", *args, success=False)
                self.assertEqual(self.calls(), [])

    def test_dry_run_does_not_install(self):
        self.installed("7.1-3")
        self.run_command("make", "-n", "dev-build", f"MAKE={self.bin / 'build-make'}", success=False)
        self.assertEqual(self.calls(), [])

    def test_msys_uses_pacman_without_sudo(self):
        self.executable(self.bin / "uname", "#!/bin/sh\nprintf 'MSYS_NT-10.0\\n'\n")
        shutil.copy2(self.bin / "dkp-pacman", self.bin / "pacman")
        (self.bin / "dkp-pacman").unlink()
        self.run_command("bash", "scripts/install_switch_ffmpeg_airplay.sh")
        self.assertEqual(self.calls(), ["install", "verify"])


if __name__ == "__main__":
    unittest.main()
