from __future__ import annotations

import unittest

from tools.release.targets import Q2AAS_TOOL_FORBIDDEN_PATHS, RELEASE_NOTICE_PATHS, get_target
from tools.release.verify_artifacts import validate_manifest


class TargetContractTests(unittest.TestCase):
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

    def test_release_notice_manifest_entries_must_be_nonempty(self) -> None:
        target = get_target("windows-x86_64")
        config = target["server"]
        notice_path = RELEASE_NOTICE_PATHS[0]
        manifest = {
            "package": {"name": config["package_name"]},
            "role": "server",
            "launch_exe": config["launch_exe"],
            "engine_library": config["engine_library"],
            "local_manifest_name": config["local_manifest_name"],
            "files": [{"path": notice_path, "size": 0}],
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
