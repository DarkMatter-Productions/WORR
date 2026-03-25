from __future__ import annotations

import unittest

from tools.release.release_index import ReleaseIndexError, resolve_role_payload


def sample_index() -> dict:
    return {
        "schema_version": 3,
        "targets": [
            {
                "platform_stub": "win64",
                "roles": {
                    "client": {
                        "role": "client",
                        "launch_exe": "worr_x86_64.exe",
                        "engine_library": "worr_engine_x86_64.dll",
                        "update_manifest_name": "worr-client-win64-update.json",
                        "update_package_name": "worr-client-win64-update.zip",
                        "local_manifest_name": "worr_install_manifest.json",
                    }
                },
            }
        ],
    }


class ReleaseIndexTests(unittest.TestCase):
    def test_resolves_role_payload(self) -> None:
        payload = resolve_role_payload(sample_index(), "win64", "client")
        self.assertEqual(payload["launch_exe"], "worr_x86_64.exe")

    def test_missing_role_payload_raises(self) -> None:
        with self.assertRaises(ReleaseIndexError):
            resolve_role_payload(sample_index(), "win64", "server")

    def test_missing_required_metadata_raises(self) -> None:
        index = sample_index()
        del index["targets"][0]["roles"]["client"]["update_package_name"]
        with self.assertRaises(ReleaseIndexError):
            resolve_role_payload(index, "win64", "client")


if __name__ == "__main__":
    unittest.main()
