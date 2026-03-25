from __future__ import annotations

import unittest

from tools.release.targets import get_target


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


if __name__ == "__main__":
    unittest.main()
