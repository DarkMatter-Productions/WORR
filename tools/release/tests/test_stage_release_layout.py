from __future__ import annotations

import pathlib
import tempfile
import unittest

from tools.release.stage_release_layout import stage_release_layout


class StageReleaseLayoutSafetyTests(unittest.TestCase):
    def test_normal_merge_and_root_relocation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            output_dir = root / "output"
            (input_dir / "basew").mkdir(parents=True)
            (input_dir / ".release" / "basew").mkdir(parents=True)
            (input_dir / "basew" / "module.dll").write_bytes(b"module")
            (input_dir / ".release" / "basew" / "extra.dat").write_bytes(b"extra")
            (input_dir / "relocate-me.exe").write_bytes(b"tool")

            stage_release_layout(
                input_dir,
                output_dir,
                relocate_root_files=["relocate-me.exe"],
                relocate_root_dir="bin",
            )

            self.assertEqual(b"module", (output_dir / "basew" / "module.dll").read_bytes())
            self.assertEqual(b"extra", (output_dir / "basew" / "extra.dat").read_bytes())
            self.assertEqual(b"tool", (output_dir / "bin" / "relocate-me.exe").read_bytes())

    def test_input_and_output_must_not_overlap_before_cleanup(self) -> None:
        cases = ("same", "nested", "output-parent")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temp:
                root = pathlib.Path(temp)
                if case == "output-parent":
                    output_dir = root / "output"
                    input_dir = output_dir / "input"
                else:
                    input_dir = root / "input"
                    output_dir = input_dir if case == "same" else input_dir / "output"
                (input_dir / "basew").mkdir(parents=True)
                sentinel = input_dir / "basew" / "sentinel.dat"
                sentinel.write_bytes(b"preserve")

                with self.assertRaises(SystemExit):
                    stage_release_layout(input_dir, output_dir)

                self.assertEqual(b"preserve", sentinel.read_bytes())

    def test_failed_staging_preserves_the_previous_complete_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            output_dir = root / "output"
            (input_dir / "basew").mkdir(parents=True)
            (input_dir / ".release" / "basew").mkdir(parents=True)
            (input_dir / "basew" / "collision.dat").write_bytes(b"first")
            (input_dir / ".release" / "basew" / "collision.dat").write_bytes(b"second")
            output_dir.mkdir()
            (output_dir / "sentinel.dat").write_bytes(b"previous complete tree")

            with self.assertRaisesRegex(SystemExit, "collision"):
                stage_release_layout(input_dir, output_dir)

            self.assertEqual(
                b"previous complete tree",
                (output_dir / "sentinel.dat").read_bytes(),
            )
            self.assertFalse((output_dir / "basew" / "collision.dat").exists())

    def test_successful_staging_replaces_the_previous_tree_as_a_unit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            output_dir = root / "output"
            (input_dir / "basew").mkdir(parents=True)
            (input_dir / "basew" / "current.dat").write_bytes(b"current")
            output_dir.mkdir()
            stale_path = output_dir / "stale.dat"
            stale_path.write_bytes(b"stale")
            stale_path.chmod(0o444)

            stage_release_layout(input_dir, output_dir)

            self.assertEqual(b"current", (output_dir / "basew" / "current.dat").read_bytes())
            self.assertFalse((output_dir / "stale.dat").exists())
            self.assertFalse(
                any(path.name.startswith(".output.backup.") for path in root.iterdir())
            )

    def test_source_links_are_rejected_before_copy(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            outside_file = root / "outside.dat"
            outside_dir = root / "outside-dir"
            outside_file.write_bytes(b"secret")
            outside_dir.mkdir()
            (outside_dir / "secret.dat").write_bytes(b"secret")

            for link_kind in ("file", "directory"):
                with self.subTest(link_kind=link_kind):
                    input_dir = root / f"input-{link_kind}"
                    output_dir = root / f"output-{link_kind}"
                    (input_dir / "basew").mkdir(parents=True)
                    link = (
                        input_dir / "linked.dat"
                        if link_kind == "file"
                        else input_dir / "basew" / "linked-dir"
                    )
                    try:
                        if link_kind == "file":
                            link.symlink_to(outside_file)
                        else:
                            link.symlink_to(outside_dir, target_is_directory=True)
                    except OSError as exc:
                        self.skipTest(f"symbolic links are unavailable: {exc}")

                    with self.assertRaises(SystemExit):
                        stage_release_layout(input_dir, output_dir)

                    self.assertFalse(output_dir.exists())

    def test_layout_names_cannot_escape_the_staging_roots(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            input_dir = root / "input"
            (input_dir / "basew").mkdir(parents=True)
            (input_dir / "basew" / "sentinel.dat").write_bytes(b"preserve")
            (input_dir / "relocate-me.exe").write_bytes(b"tool")

            absolute_escape = str(root / "absolute-escape")
            cases = (
                {"base_game": "../escape"},
                {"base_game": absolute_escape},
                {"release_game": "../escape"},
                {"release_game": absolute_escape},
                {"relocate_root_dir": "../escape"},
                {"relocate_root_dir": absolute_escape},
            )
            for index, kwargs in enumerate(cases):
                with self.subTest(**kwargs):
                    output_dir = root / f"output-{index}"
                    with self.assertRaises(SystemExit):
                        stage_release_layout(
                            input_dir,
                            output_dir,
                            relocate_root_files=["relocate-me.exe"],
                            **kwargs,
                        )
                    self.assertFalse(output_dir.exists())
                    self.assertEqual(
                        b"preserve",
                        (input_dir / "basew" / "sentinel.dat").read_bytes(),
                    )


if __name__ == "__main__":
    unittest.main()
