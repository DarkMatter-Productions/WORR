import unittest

from tools import run_fast_headless_tests as fast_tests


class FastHeadlessTestsTest(unittest.TestCase):
    def test_selects_only_positive_timeouts_at_or_below_limit(self) -> None:
        tests = [
            {"name": "slow", "timeout": 31},
            {"name": "fast-b", "timeout": 30},
            {"name": "unset", "timeout": None},
            {"name": "fast-a", "timeout": 1},
            {"name": "invalid", "timeout": 0},
        ]

        selected = fast_tests._select_tests(tests, 30)

        self.assertEqual([test["name"] for test in selected], ["fast-a", "fast-b"])

    def test_headless_guard_rejects_direct_interactive_runtime(self) -> None:
        for runtime in (
            "/tmp/WORR.exe",
            "/tmp/worr_x86_64",
            r"C:\WORR\worr_x86_64.exe",
        ):
            with self.subTest(runtime=runtime):
                with self.assertRaisesRegex(ValueError, "interactive runtime"):
                    fast_tests._assert_headless(
                        [{"name": "bad-client-test", "cmd": [runtime]}]
                    )

    def test_headless_guard_accepts_unit_binary_and_python_contract(self) -> None:
        fast_tests._assert_headless(
            [
                {"name": "native-unit", "cmd": ["/tmp/model_test"]},
                {
                    "name": "source-contract",
                    "cmd": ["/usr/bin/python3", "tools/test_contract.py"],
                },
            ]
        )


if __name__ == "__main__":
    unittest.main()
