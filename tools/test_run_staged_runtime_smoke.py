from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from tools import run_staged_runtime_smoke as smoke


class _FakeProcess:
    def __init__(
        self,
        *,
        ready_path: Path,
        ready_token: str,
        returncode: int = 0,
        write_ready: bool = True,
    ) -> None:
        self.ready_path = ready_path
        self.ready_token = ready_token
        self.returncode = returncode
        self.write_ready = write_ready

    def communicate(self, timeout: int) -> tuple[str, str]:
        if self.write_ready:
            self.ready_path.write_text(self.ready_token, encoding="utf-8")
        return "dedicated stdout", ""


class StagedRuntimeSmokeTests(unittest.TestCase):
    def test_command_is_dedicated_input_free_and_uses_isolated_home(self) -> None:
        command = smoke.build_command(
            Path("/stage/worr_ded_x86_64"),
            Path("/stage"),
            Path("/scratch/home"),
        )

        self.assertEqual(str(Path("/stage/worr_ded_x86_64")), command[0])
        self.assertIn("--bootstrap-skip-update-check", command)
        self.assertIn("--bootstrap-quiet-status", command)
        self.assertEqual(
            str(Path("/scratch/home")), command[command.index("homedir") + 1]
        )
        self.assertEqual("0", command[command.index("net_port") + 1])
        self.assertEqual(["+wait", "2", "+quit"], command[-3:])
        self.assertNotIn("worr_x86_64", command[0])

    def test_success_requires_the_engine_owned_ready_token(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ready_path = root / "engine-ready.token"
            token = "a" * 64
            process = _FakeProcess(ready_path=ready_path, ready_token=token)
            with mock.patch.object(
                smoke.headless_process,
                "start_headless_process",
                return_value=process,
            ) as start, mock.patch.object(
                smoke.headless_process,
                "terminate_process_tree",
            ) as terminate:
                smoke.run_smoke(
                    launcher=Path("/stage/worr_ded_x86_64"),
                    install_dir=Path("/stage"),
                    runtime_root=root,
                    timeout_seconds=7,
                    ready_token=token,
                )
                self.assertTrue((root / "home" / "basew").is_dir())

        launch_kwargs = start.call_args.kwargs
        self.assertIs(launch_kwargs["stdin"], subprocess.DEVNULL)
        self.assertEqual(Path("/stage"), launch_kwargs["cwd"])
        self.assertEqual(token, launch_kwargs["env"]["WORR_BOOTSTRAP_READY_TOKEN"])
        self.assertEqual(str(ready_path), launch_kwargs["env"]["WORR_BOOTSTRAP_READY_FILE"])
        terminate.assert_called_once_with(process)

    def test_any_install_tree_mutation_fails_the_smoke(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            install_dir = root / "stage"
            install_dir.mkdir()
            ready_path = root / "runtime" / "engine-ready.token"
            token = "c" * 64
            process = _FakeProcess(ready_path=ready_path, ready_token=token)

            def mutate_stage(*args: object, **kwargs: object) -> _FakeProcess:
                (install_dir / "unexpected.log").write_text("changed", encoding="utf-8")
                return process

            with mock.patch.object(
                smoke.headless_process,
                "start_headless_process",
                side_effect=mutate_stage,
            ), mock.patch.object(
                smoke.headless_process,
                "terminate_process_tree",
            ):
                with self.assertRaisesRegex(
                    smoke.StagedRuntimeSmokeError,
                    r"modified the immutable install tree: added:unexpected\.log",
                ):
                    smoke.run_smoke(
                        launcher=install_dir / "worr_ded_x86_64",
                        install_dir=install_dir,
                        runtime_root=root / "runtime",
                        timeout_seconds=7,
                        ready_token=token,
                    )

    def test_missing_ready_token_fails_even_after_a_zero_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            process = _FakeProcess(
                ready_path=root / "engine-ready.token",
                ready_token="b" * 64,
                write_ready=False,
            )
            with mock.patch.object(
                smoke.headless_process,
                "start_headless_process",
                return_value=process,
            ), mock.patch.object(
                smoke.headless_process,
                "terminate_process_tree",
            ):
                with self.assertRaisesRegex(
                    smoke.StagedRuntimeSmokeError,
                    "did not create its readiness token",
                ):
                    smoke.run_smoke(
                        launcher=Path("/stage/worr_ded_x86_64"),
                        install_dir=Path("/stage"),
                        runtime_root=root,
                        timeout_seconds=7,
                        ready_token="b" * 64,
                    )

    def test_platform_contract_uses_only_staged_server_artifacts(self) -> None:
        root = Path("stage")
        paths = smoke._required_staged_paths(root, "windows-x86_64")

        self.assertEqual(root / "worr_ded_x86_64.exe", paths[0])
        self.assertEqual(root / "worr_ded_engine_x86_64.dll", paths[1])
        self.assertEqual(root / "basew" / "sgame_x86_64.dll", paths[2])
        self.assertEqual(root / "basew" / "pak0.pkz", paths[3])
        self.assertEqual(root / "worr_install.lock", paths[4])
        self.assertTrue(all("worr_engine_x86_64" not in str(path) for path in paths))


class PullRequestWorkflowRuntimeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = (
            Path(__file__).resolve().parents[1]
            / ".github"
            / "workflows"
            / "pull-request.yml"
        ).read_text(encoding="utf-8")

    def test_renderer_lanes_are_independent_external_library_targets(self) -> None:
        self.assertNotIn("matrix.renderer", self.workflow)
        for job, label, build_dir, target in (
            ("renderer-opengl", "OpenGL", "builddir-renderer", "worr_opengl_x86_64"),
            ("renderer-vulkan", "native Vulkan shaders", "builddir-vulkan", "worr_vulkan_x86_64"),
            ("renderer-rtx", "RTX/VKPT shaders", "builddir-rtx", "worr_rtx_x86_64"),
        ):
            self.assertIn(f"  {job}:\n", self.workflow)
            self.assertIn(f"name: External renderer — {label}", self.workflow)
            self.assertIn(f"meson compile -C {build_dir} {target}", self.workflow)
        self.assertGreaterEqual(self.workflow.count("-Dexternal-renderers=true"), 3)
        self.assertGreaterEqual(self.workflow.count("-Dvulkan=enabled"), 2)

    def test_every_platform_runs_the_dedicated_smoke_after_staging(self) -> None:
        self.assertEqual(2, self.workflow.count("tools/run_staged_runtime_smoke.py"))
        self.assertIn("Smoke staged dedicated runtime (Linux/macOS)", self.workflow)
        self.assertIn("Smoke staged dedicated runtime (Windows)", self.workflow)
        self.assertLess(
            self.workflow.index("Refresh staged install (Linux/macOS)"),
            self.workflow.index("Smoke staged dedicated runtime (Linux/macOS)"),
        )
        self.assertLess(
            self.workflow.index("Smoke staged dedicated runtime (Windows)"),
            self.workflow.index("Run bounded headless Meson tests (Linux/macOS)"),
        )


if __name__ == "__main__":
    unittest.main()
