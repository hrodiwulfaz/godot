from __future__ import annotations

import importlib.util
import tempfile
import unittest
from unittest import mock
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "outpostia_build.py"
SPEC = importlib.util.spec_from_file_location("outpostia_build", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
OUTPOSTIA_BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OUTPOSTIA_BUILD)


class BuildNamespaceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.sha = "a" * 40
        self.toolchain = {
            "host": {"system": "test"},
            "python": {"sha256": "python"},
            "environment": {"CC": "compiler"},
            "executables": [{"path": "compiler", "sha256": "compiler"}],
        }

    def ids(
        self,
        *,
        sha: str | None = None,
        platform: str = "windows",
        target: str = "editor",
        flags: list[str] | None = None,
        custom_config_sha: str = "config",
        toolchain: dict | None = None,
    ) -> tuple[str, str]:
        output_id, cache_id, _ = OUTPOSTIA_BUILD.build_namespace_ids(
            sha or self.sha,
            platform,
            target,
            flags or ["module_gdscript_enabled=no"],
            custom_config_sha,
            toolchain or self.toolchain,
        )
        return output_id, cache_id

    def test_identical_builds_reuse_output_and_cache_namespaces(self) -> None:
        self.assertEqual(self.ids(), self.ids())

    def test_changed_sha_selects_new_output_and_compatible_cache(self) -> None:
        output_id, cache_id = self.ids()
        changed_output_id, changed_cache_id = self.ids(sha="b" * 40)

        self.assertNotEqual(output_id, changed_output_id)
        self.assertEqual(cache_id, changed_cache_id)

    def test_target_platform_toolchain_flags_and_config_isolate_namespaces(self) -> None:
        original = self.ids()
        changed_toolchain = {**self.toolchain, "executables": [{"sha256": "changed"}]}
        variants = [
            self.ids(target="template_release"),
            self.ids(platform="linuxbsd"),
            self.ids(toolchain=changed_toolchain),
            self.ids(flags=["module_gdscript_enabled=yes"]),
            self.ids(custom_config_sha="changed"),
        ]

        for variant in variants:
            with self.subTest(variant=variant):
                self.assertNotEqual(original[0], variant[0])
                self.assertNotEqual(original[1], variant[1])

    def test_path_only_changes_reuse_namespaces_when_tools_are_unchanged(self) -> None:
        executable = {"path": "compiler", "size": 1, "sha256": "compiler"}
        with mock.patch.object(
            OUTPOSTIA_BUILD, "executable_identity", return_value=executable
        ):
            with mock.patch.dict(OUTPOSTIA_BUILD.os.environ, {"PATH": "first;second"}, clear=True):
                first = OUTPOSTIA_BUILD.toolchain_identity(
                    "scons", OUTPOSTIA_BUILD.WINDOWS_PLATFORM
                )
            with mock.patch.dict(OUTPOSTIA_BUILD.os.environ, {"PATH": "second;first"}, clear=True):
                second = OUTPOSTIA_BUILD.toolchain_identity(
                    "scons", OUTPOSTIA_BUILD.WINDOWS_PLATFORM
                )

        self.assertNotIn("PATH", first["environment"])
        self.assertEqual(self.ids(toolchain=first), self.ids(toolchain=second))

    def test_changed_resolved_tool_identity_selects_new_namespaces(self) -> None:
        changed_toolchain = {
            **self.toolchain,
            "executables": [{"path": "other-compiler", "sha256": "changed"}],
        }

        self.assertNotEqual(self.ids(), self.ids(toolchain=changed_toolchain))

    def test_extra_flags_are_sorted_and_pipeline_owned_flags_are_rejected(self) -> None:
        self.assertEqual(
            OUTPOSTIA_BUILD.normalized_scons_flags(["tests=yes", "dev_build=no"]),
            ["dev_build=no", "tests=yes"],
        )
        with self.assertRaises(OUTPOSTIA_BUILD.PipelineError):
            OUTPOSTIA_BUILD.normalized_scons_flags(["target=editor"])
        with self.assertRaises(OUTPOSTIA_BUILD.PipelineError):
            OUTPOSTIA_BUILD.normalized_scons_flags(["tests=yes", "tests=no"])

    def test_worktree_commands_append_process_local_safe_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            worktree = Path(temporary_directory)
            worktree.joinpath(".git").write_text("gitdir: test", encoding="utf-8")
            environment = OUTPOSTIA_BUILD.command_environment(
                worktree,
                {
                    "GIT_CONFIG_COUNT": "1",
                    "GIT_CONFIG_KEY_0": "existing",
                    "GIT_CONFIG_VALUE_0": "value",
                },
            )

        self.assertEqual(environment["GIT_CONFIG_COUNT"], "2")
        self.assertEqual(environment["GIT_CONFIG_KEY_0"], "existing")
        self.assertEqual(environment["GIT_CONFIG_VALUE_0"], "value")
        self.assertEqual(environment["GIT_CONFIG_KEY_1"], "safe.directory")
        self.assertEqual(environment["GIT_CONFIG_VALUE_1"], worktree.as_posix())


class PruneBuildNamespaceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.repository = self.root / "repository"
        self.repository.mkdir()
        self.git(self.repository, "init", "-q", "-b", "main")
        self.git(self.repository, "config", "user.email", "test@example.invalid")
        self.git(self.repository, "config", "user.name", "Test")
        self.commits = []
        for index in range(5):
            self.repository.joinpath("file.txt").write_text(f"{index}" + chr(10), encoding="utf-8")
            self.git(self.repository, "add", "file.txt")
            self.git(self.repository, "commit", "-q", "-m", f"commit {index}")
            self.commits.append(self.git(self.repository, "rev-parse", "HEAD"))
        self.build_root = self.root / "Builds"

    def tearDown(self) -> None:
        for worktree in self.build_root.rglob("source"):
            if worktree.joinpath(".git").is_file():
                self.git(self.repository, "worktree", "remove", "--force", str(worktree))
        self.temporary_directory.cleanup()

    def git(self, directory: Path, *arguments: str) -> str:
        return OUTPOSTIA_BUILD.run_capture(OUTPOSTIA_BUILD.git_command(directory, *arguments), "test git", directory)

    def namespace(self, commit: str, age: int, platform: str = "windows", target: str = "editor") -> Path:
        namespace_root = self.build_root / platform / target / f"{commit[:12]}-{age:04d}"
        self.git(self.repository, "worktree", "add", "-q", "--detach", str(namespace_root / "source"), commit)
        metadata = namespace_root / "namespace.json"
        metadata.write_text("{}", encoding="utf-8")
        timestamp = 1_800_000_000 - age * 60
        OUTPOSTIA_BUILD.os.utime(metadata, (timestamp, timestamp))
        return namespace_root

    def prune(self, keep: int, current: Path, dry_run: bool = False, platform: str = "windows", target: str = "editor") -> list[Path]:
        with mock.patch.object(OUTPOSTIA_BUILD, "log"):
            return OUTPOSTIA_BUILD.prune_build_namespaces(self.build_root, platform, target, keep, current, dry_run, self.repository)

    def test_keeps_the_current_and_the_newest_others(self) -> None:
        current = self.namespace(self.commits[4], 0)
        newest_old = self.namespace(self.commits[3], 1)
        older = self.namespace(self.commits[2], 2)
        oldest = self.namespace(self.commits[1], 3)

        removed = self.prune(2, current)

        self.assertEqual(sorted(removed), sorted([older, oldest]))
        self.assertTrue(current.joinpath("source", "file.txt").is_file())
        self.assertTrue(newest_old.joinpath("source", "file.txt").is_file())
        self.assertFalse(older.exists())
        self.assertFalse(oldest.exists())
        self.assertNotIn(older.name, self.git(self.repository, "worktree", "list"))

    def test_the_current_namespace_stays_even_when_it_is_not_the_newest(self) -> None:
        current = self.namespace(self.commits[4], 9)
        newer = self.namespace(self.commits[3], 1)

        removed = self.prune(1, current)

        self.assertEqual(removed, [newer])
        self.assertTrue(current.is_dir())

    def test_zero_keeps_everything(self) -> None:
        current = self.namespace(self.commits[4], 0)
        old = self.namespace(self.commits[1], 5)

        self.assertEqual(self.prune(0, current), [])
        self.assertTrue(old.joinpath("source", "file.txt").is_file())

    def test_a_dry_run_lists_without_removing(self) -> None:
        current = self.namespace(self.commits[4], 0)
        old = self.namespace(self.commits[1], 5)

        self.assertEqual(self.prune(1, current, dry_run=True), [old])
        self.assertTrue(old.joinpath("source", "file.txt").is_file())

    def loose_commit(self, text: str) -> str:
        self.git(self.repository, "checkout", "-q", "--detach", self.commits[4])
        self.repository.joinpath("file.txt").write_text(text + chr(10), encoding="utf-8")
        self.git(self.repository, "commit", "-q", "-am", text)
        commit = self.git(self.repository, "rev-parse", "HEAD")
        self.git(self.repository, "checkout", "-q", "main")
        return commit

    def test_tracked_changes_stay_and_take_no_slot(self) -> None:
        current = self.namespace(self.commits[4], 0)
        changed = self.namespace(self.commits[3], 1)
        changed.joinpath("source", "file.txt").write_text("changed" + chr(10), encoding="utf-8")
        tagged_commit = self.git(self.repository, "commit-tree", "-p", self.commits[1], "-m", "tagged", self.git(self.repository, "rev-parse", f"{self.commits[1]}^{{tree}}"))
        self.git(self.repository, "tag", "kept-by-tag", tagged_commit)
        tagged = self.namespace(tagged_commit, 2)
        oldest = self.namespace(self.commits[0], 3)

        removed = self.prune(2, current)

        self.assertEqual(removed, [oldest])
        self.assertTrue(changed.joinpath("source", "file.txt").is_file())
        self.assertTrue(tagged.joinpath("source", "file.txt").is_file())

    def test_commits_on_no_branch_or_tag_take_no_slot_and_go_once_older_than_the_kept_ones(self) -> None:
        current = self.namespace(self.commits[4], 0)
        newer_loose_commit = self.loose_commit("newer loose")
        newer_loose = self.namespace(newer_loose_commit, 1)
        previous = self.namespace(self.commits[3], 2)
        older_loose_commit = self.loose_commit("older loose")
        older_loose = self.namespace(older_loose_commit, 3)
        oldest = self.namespace(self.commits[1], 4)

        removed = self.prune(2, current)

        self.assertEqual(sorted(removed), sorted([older_loose, oldest]))
        self.assertTrue(previous.joinpath("source", "file.txt").is_file())
        self.assertTrue(newer_loose.joinpath("source", "file.txt").is_file())
        self.assertEqual(self.git(self.repository, "rev-parse", f"backup/build-{older_loose_commit[:12]}"), older_loose_commit)
        self.assertEqual(self.git(self.repository, "branch", "--list", f"backup/build-{newer_loose_commit[:12]}"), "")

    def test_a_kept_backup_branch_is_reused(self) -> None:
        current = self.namespace(self.commits[4], 0)
        loose_commit = self.loose_commit("loose")
        self.git(self.repository, "branch", f"backup/build-{loose_commit[:12]}", loose_commit)
        loose = self.namespace(loose_commit, 1)

        self.assertEqual(self.prune(1, current), [loose])
        self.assertEqual(self.git(self.repository, "rev-parse", f"backup/build-{loose_commit[:12]}"), loose_commit)

    def test_a_folder_that_cannot_be_removed_is_skipped(self) -> None:
        current = self.namespace(self.commits[4], 0)
        locked = self.namespace(self.commits[2], 1)
        self.git(self.repository, "worktree", "lock", str(locked / "source"))
        old = self.namespace(self.commits[1], 2)

        removed = self.prune(1, current)

        self.assertEqual(removed, [old])
        self.assertTrue(locked.joinpath("source", "file.txt").is_file())
        self.git(self.repository, "worktree", "unlock", str(locked / "source"))

    def test_other_platforms_targets_and_the_cache_are_not_touched(self) -> None:
        current = self.namespace(self.commits[4], 0)
        template = self.namespace(self.commits[1], 5, target="template_release")
        linux = self.namespace(self.commits[1], 6, platform="linuxbsd")
        cache = self.root / "SConsCache"
        cache.mkdir()
        cache.joinpath("entry").write_text("cached", encoding="utf-8")

        self.assertEqual(self.prune(1, current), [])
        self.assertTrue(template.joinpath("source", "file.txt").is_file())
        self.assertTrue(linux.joinpath("source", "file.txt").is_file())
        self.assertTrue(cache.joinpath("entry").is_file())

    def test_a_folder_without_a_worktree_stays(self) -> None:
        current = self.namespace(self.commits[4], 0)
        stray = self.build_root / "windows" / "editor" / "stray"
        stray.mkdir(parents=True)
        stray.joinpath("namespace.json").write_text("{}", encoding="utf-8")

        self.assertEqual(self.prune(1, current), [])
        self.assertTrue(stray.joinpath("namespace.json").is_file())

    def test_negative_keep_builds_is_refused(self) -> None:
        with mock.patch.object(OUTPOSTIA_BUILD.sys, "argv", ["outpostia_build.py", "windows", "--keep-builds", "-1", "--dry-run"]):
            with self.assertRaises(OUTPOSTIA_BUILD.PipelineError):
                OUTPOSTIA_BUILD.main()


if __name__ == "__main__":
    unittest.main()
