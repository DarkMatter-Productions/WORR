from __future__ import annotations

import unittest

from tools.release.targets import (
    Q2AAS_TOOL_FORBIDDEN_PATHS,
    RELEASE_NOTICE_PATHS,
    RUNTIME_COORDINATION_LOCK,
    TARGETS,
    USER_DATA_FORBIDDEN_PATHS,
    client_renderer_required_paths,
    game_module_required_path,
    get_target,
)
from tools.release.verify_artifacts import validate_manifest


class TargetContractTests(unittest.TestCase):
    def test_every_runtime_payload_requires_the_stable_coordination_lock(self) -> None:
        for target in TARGETS:
            with self.subTest(platform_id=target["platform_id"]):
                for role in ("client", "server"):
                    config = target[role]
                    self.assertIn(RUNTIME_COORDINATION_LOCK, config["include"])
                    self.assertIn(RUNTIME_COORDINATION_LOCK, config["required_paths"])
                    self.assertIn(
                        RUNTIME_COORDINATION_LOCK,
                        config["update_include"],
                    )
                    self.assertIn(
                        RUNTIME_COORDINATION_LOCK,
                        config["update_required_paths"],
                    )

    def test_client_and_full_update_payloads_require_every_renderer(self) -> None:
        for target in TARGETS:
            with self.subTest(platform_id=target["platform_id"]):
                for renderer_path in client_renderer_required_paths(target):
                    self.assertIn(renderer_path, target["client"]["required_paths"])
                    for role in ("client", "server"):
                        self.assertIn(renderer_path, target[role]["update_required_paths"])

    def test_debug_or_decoy_renderer_files_do_not_satisfy_the_runtime_contract(self) -> None:
        target = get_target("windows-x86_64")
        config = target["client"]
        renderer_paths = client_renderer_required_paths(target)
        manifest = {
            "package": {
                "name": config["package_name"],
                "size": 1,
                "sha256": "a" * 64,
            },
            "role": "client",
            "launch_exe": config["launch_exe"],
            "engine_library": config["engine_library"],
            "local_manifest_name": config["local_manifest_name"],
            "files": [
                {"path": f"{path}.pdb", "size": 1, "sha256": "b" * 64}
                for path in renderer_paths
            ],
        }
        failures: list[str] = []

        validate_manifest(
            failures,
            target,
            "client",
            manifest,
            manifest_name=config["manifest_name"],
            package_name=config["package_name"],
            required_paths=renderer_paths,
            forbidden_paths=[],
        )

        for renderer_path in renderer_paths:
            self.assertIn(
                f"windows-x86_64 client: manifest missing required path {renderer_path}",
                failures,
            )

    def test_game_module_requirements_use_exact_runtime_library_names(self) -> None:
        for target in TARGETS:
            with self.subTest(platform_id=target["platform_id"]):
                cgame_path = game_module_required_path(target, "cgame")
                sgame_path = game_module_required_path(target, "sgame")
                self.assertIn(cgame_path, target["client"]["required_paths"])
                self.assertIn(sgame_path, target["client"]["required_paths"])
                self.assertIn(sgame_path, target["server"]["required_paths"])
                for role in ("client", "server"):
                    self.assertIn(cgame_path, target[role]["update_required_paths"])
                    self.assertIn(sgame_path, target[role]["update_required_paths"])
                self.assertNotIn("basew/cgame*", target["client"]["required_paths"])
                self.assertNotIn("basew/sgame*", target["server"]["required_paths"])

    def test_debug_or_test_game_modules_do_not_satisfy_the_runtime_contract(self) -> None:
        target = get_target("windows-x86_64")
        config = target["client"]
        module_paths = [
            game_module_required_path(target, "cgame"),
            game_module_required_path(target, "sgame"),
        ]
        manifest = {
            "package": {"name": config["package_name"], "size": 1, "sha256": "a" * 64},
            "role": "client",
            "launch_exe": config["launch_exe"],
            "engine_library": config["engine_library"],
            "local_manifest_name": config["local_manifest_name"],
            "files": [
                {"path": "basew/cgame_x86_64.pdb", "size": 1, "sha256": "b" * 64},
                {"path": "basew/sgame_runtime_test.dll", "size": 1, "sha256": "c" * 64},
            ],
        }
        failures: list[str] = []

        validate_manifest(
            failures,
            target,
            "client",
            manifest,
            manifest_name=config["manifest_name"],
            package_name=config["package_name"],
            required_paths=module_paths,
            forbidden_paths=[],
        )

        for module_path in module_paths:
            self.assertIn(
                f"windows-x86_64 client: manifest missing required path {module_path}",
                failures,
            )

    def test_update_payloads_cover_full_install_tree(self) -> None:
        target = get_target("windows-x86_64")
        client = target["client"]

        self.assertIn(target["server"]["launch_exe"], client["update_required_paths"])
        self.assertIn(target["server"]["engine_library"], client["update_required_paths"])
        self.assertIn(target["server"]["launch_exe"].replace(".exe", "*"), client["update_include"])
        self.assertIn("worr_update.json", client["update_required_paths"])

    def test_manual_server_payload_stays_split(self) -> None:
        target = get_target("windows-x86_64")
        server = target["server"]

        self.assertIn(target["client"]["launch_exe"], server["forbidden_paths"])
        self.assertIn(target["client"]["engine_library"], server["forbidden_paths"])
        self.assertNotIn(target["client"]["launch_exe"], server["required_paths"])

    def test_release_notice_bundle_is_required_for_binary_payloads(self) -> None:
        target = get_target("windows-x86_64")

        for role in ("client", "server"):
            config = target[role]
            self.assertIn("licenses/*", config["include"])
            self.assertIn("licenses/*", config["update_include"])
            for notice_path in RELEASE_NOTICE_PATHS:
                self.assertIn(notice_path, config["required_paths"])
                self.assertIn(notice_path, config["update_required_paths"])

    def test_q2aas_tool_binaries_are_forbidden_from_release_payloads(self) -> None:
        target = get_target("windows-x86_64")

        for role in ("client", "server"):
            config = target[role]
            for pattern in Q2AAS_TOOL_FORBIDDEN_PATHS:
                self.assertIn(pattern, config["forbidden_paths"])
                self.assertIn(pattern, config["update_forbidden_paths"])

    def test_user_generated_data_is_never_a_release_payload_candidate(self) -> None:
        for target in TARGETS:
            with self.subTest(platform_id=target["platform_id"]):
                for role in ("client", "server"):
                    config = target[role]
                    for pattern in USER_DATA_FORBIDDEN_PATHS:
                        self.assertIn(pattern, config["exclude"])
                        self.assertIn(pattern, config["forbidden_paths"])
                        self.assertIn(pattern, config["update_exclude"])
                        self.assertIn(pattern, config["update_forbidden_paths"])

    def test_release_notice_manifest_entries_must_be_nonempty(self) -> None:
        target = get_target("windows-x86_64")
        config = target["server"]
        notice_path = RELEASE_NOTICE_PATHS[0]
        manifest = {
            "package": {"name": config["package_name"], "size": 1, "sha256": "a" * 64},
            "role": "server",
            "launch_exe": config["launch_exe"],
            "engine_library": config["engine_library"],
            "local_manifest_name": config["local_manifest_name"],
            "files": [{"path": notice_path, "size": 0, "sha256": "b" * 64}],
        }
        failures: list[str] = []

        validate_manifest(
            failures,
            target,
            "server",
            manifest,
            manifest_name=config["manifest_name"],
            package_name=config["package_name"],
            required_paths=[notice_path],
            forbidden_paths=[],
        )

        self.assertIn(f"windows-x86_64 server: manifest has empty release notice {notice_path}", failures)


if __name__ == "__main__":
    unittest.main()
