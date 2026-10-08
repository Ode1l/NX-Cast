#!/usr/bin/env python3
"""Local fixtures only: no Switch upload, package install or GitHub push."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nxcast-workflow-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "project"
        self.scripts = self.root / "scripts"
        self.scripts.mkdir(parents=True)
        for name in ("dev.sh", "dev_environment.sh", "run_nxlink.sh", "publish_release.sh"):
            shutil.copy2(ROOT / "scripts" / name, self.scripts / name)
        self.bin = self.base / "bin"
        self.bin.mkdir()
        self.sdk = self.base / "SDK with spaces"
        self.sdk.mkdir()
        (self.sdk / "switchvars.sh").write_text('export NXCAST_FIXTURE_SDK=1\n')
        self.calls = self.base / "calls"
        self.env = dict(os.environ, DEVKITPRO=str(self.sdk),
                        PATH=f"{self.bin}{os.pathsep}{os.environ['PATH']}",
                        FIXTURE_CALLS=str(self.calls), TRACE_MEDIA="1", TRACE_INPUT="1", TRACE_AIRPLAY="1")
        self.env.pop("BUILD_JOBS", None)
        self.executable("make", '#!/bin/sh\n[ "$NXCAST_FIXTURE_SDK" = 1 ] || exit 90\nprintf "%s\\n" "$*" >> "$FIXTURE_CALLS"\n')
        self.executable("nxlink", '#!/bin/sh\nprintf "%s\\n" "$*" >> "$FIXTURE_CALLS"\nexit "${FIXTURE_UPLOAD_EXIT:-0}"\n')
        self.env["NXLINK_BIN"] = str(self.bin / "nxlink")
        (self.root / "NX-Cast.nro").write_bytes(b"fixture")

    def executable(self, name, content):
        path = self.bin / name
        path.write_text(content)
        path.chmod(0o755)

    def run_command(self, *args, ok=True, cwd=None):
        result = subprocess.run(args, cwd=cwd or self.root, env=self.env,
                                text=True, capture_output=True)
        if ok:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_normal_trace_and_upload_commands(self):
        self.run_command("bash", "scripts/dev.sh", "clean")
        self.run_command("bash", "scripts/dev.sh", "build")
        self.run_command("bash", "scripts/dev.sh", "trace")
        self.run_command("bash", "scripts/dev.sh", "upload")
        self.run_command("bash", "scripts/dev.sh", "upload-log")
        calls = self.calls.read_text().splitlines()
        self.assertEqual(calls[:3], ["clean",
            "dev-build BUILD_JOBS=4 TRACE_MEDIA=0 TRACE_INPUT=0 TRACE_AIRPLAY=0",
            "full-trace-build BUILD_JOBS=4 TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1"])
        self.assertNotIn(" -s ", " " + calls[3] + " ")
        self.assertIn(" -s ", " " + calls[4] + " ")
        self.assertEqual(len(calls), 5)
        self.env["FIXTURE_UPLOAD_EXIT"] = "7"
        result = self.run_command("bash", "scripts/dev.sh", "upload", ok=False)
        self.assertEqual(result.returncode, 7)

    def test_missing_sdk_stops_build(self):
        self.env["DEVKITPRO"] = str(self.base / "absent")
        self.run_command("bash", "scripts/dev.sh", "build", ok=False)
        self.assertFalse(self.calls.exists())

    def test_msys_normalizes_sdk_path(self):
        self.executable("uname", "#!/bin/sh\nprintf 'MSYS_NT-10.0\\n'\n")
        self.executable("cygpath", '#!/bin/sh\n[ "$1" = -u ] && [ "$2" = C:/devkitPro ] || exit 91\nprintf "%s\\n" "$FIXTURE_SDK"\n')
        self.env["FIXTURE_SDK"] = str(self.sdk)
        self.env["DEVKITPRO"] = "C:/devkitPro"
        self.run_command("bash", "scripts/dev.sh", "build")
        self.assertIn("dev-build", self.calls.read_text())

    def release_fixture(self):
        (self.root / "makefile").write_text("APP_VERSION := 0.0.1\n")
        (self.root / ".github").mkdir()
        (self.root / ".github/release-notes.md").write_text("# NX-Cast v0.0.1 Release Notes\n")
        self.remote = self.base / "remote.git"
        self.run_command("git", "init", "--bare", "--initial-branch=main", str(self.remote))
        self.run_command("git", "init", "--initial-branch=main")
        self.run_command("git", "config", "user.name", "Fixture")
        self.run_command("git", "config", "user.email", "fixture@example.invalid")
        self.run_command("git", "config", "commit.gpgsign", "false")
        self.run_command("git", "config", "tag.gpgsign", "false")
        self.run_command("git", "add", ".")
        self.run_command("git", "commit", "-m", "fixture")
        self.run_command("git", "remote", "add", "origin", str(self.remote))
        self.env["DEVKITPRO"] = str(self.base / "absent")

    def test_publish_and_existing_remote_tag(self):
        self.release_fixture()
        self.run_command("bash", "scripts/dev.sh", "publish")
        head = self.run_command("git", "rev-parse", "HEAD").stdout.strip()
        remote_head = self.run_command("git", "--git-dir", str(self.remote), "rev-parse", "v0.0.1^{commit}").stdout.strip()
        self.assertEqual(head, remote_head)
        rejected = self.run_command("bash", "scripts/dev.sh", "publish", ok=False)
        self.assertIn("already exists remotely", rejected.stderr)

    def test_publish_accepts_crlf_metadata(self):
        self.release_fixture()
        (self.root / "makefile").write_bytes(b"APP_VERSION := 0.0.1\r\n")
        (self.root / ".github/release-notes.md").write_bytes(b"# NX-Cast v0.0.1 Release Notes\r\n")
        self.run_command("git", "add", ".")
        self.run_command("git", "commit", "-m", "CRLF metadata")
        self.run_command("bash", "scripts/dev.sh", "publish")

    def test_publish_rejects_dirty_tree_and_wrong_notes(self):
        self.release_fixture()
        notes = self.root / ".github/release-notes.md"
        notes.write_text("old release notes\n")
        self.assertIn("Commit your changes", self.run_command("bash", "scripts/dev.sh", "publish", ok=False).stderr)
        self.run_command("git", "add", ".")
        self.run_command("git", "commit", "-m", "bad notes fixture")
        self.assertIn("release-notes.md", self.run_command("bash", "scripts/dev.sh", "publish", ok=False).stderr)

    def test_publish_rejects_non_main_and_conflicting_local_tag(self):
        self.release_fixture()
        self.run_command("git", "checkout", "-b", "feature")
        self.assertIn("Switch to main", self.run_command("bash", "scripts/dev.sh", "publish", ok=False).stderr)
        self.run_command("git", "checkout", "main")
        self.run_command("git", "tag", "v0.0.1")
        self.run_command("git", "commit", "--allow-empty", "-m", "new commit")
        self.assertIn("different commit", self.run_command("bash", "scripts/dev.sh", "publish", ok=False).stderr)

    def test_rejected_main_does_not_push_orphan_tag(self):
        self.release_fixture()
        self.run_command("git", "push", "origin", "main")
        self.run_command("git", "commit", "--allow-empty", "-m", "remote advance")
        self.run_command("git", "push", "origin", "main")
        # Move only the disposable fixture back to simulate a behind-main publisher.
        self.run_command("git", "update-ref", "refs/heads/main", "HEAD^")
        self.run_command("bash", "scripts/dev.sh", "publish", ok=False)
        self.run_command("git", "--git-dir", str(self.remote), "show-ref", "--verify", "refs/tags/v0.0.1", ok=False)

    def test_vscode_references(self):
        tasks = {t["label"]: t for t in json.loads((ROOT / ".vscode/tasks.json").read_text())["tasks"]}
        launches = json.loads((ROOT / ".vscode/launch.json").read_text())["configurations"]
        self.assertEqual(len(launches), 4)
        for launch in launches:
            self.assertIn(launch["preLaunchTask"], tasks)
        for task in tasks.values():
            for dependency in task.get("dependsOn", []):
                self.assertIn(dependency, tasks)
            if task.get("type") == "process":
                self.assertEqual(task["args"][-1], task["windows"]["args"][-1])


if __name__ == "__main__":
    unittest.main()
