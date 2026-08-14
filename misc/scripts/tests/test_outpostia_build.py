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


if __name__ == "__main__":
    unittest.main()
