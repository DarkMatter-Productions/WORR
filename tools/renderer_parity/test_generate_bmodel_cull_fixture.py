#!/usr/bin/env python3
"""Regression checks for the mixed visible/off-screen bmodel fixture."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
import sys

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
import generate_bmodel_cull_fixture as fixture


class BmodelCullFixtureTests(unittest.TestCase):
    def test_authored_culling_map_is_current(self) -> None:
        asset_root = Path(__file__).resolve().parents[2] / "assets"
        for path, data in fixture.generated_outputs(asset_root).items():
            self.assertEqual(data, path.read_bytes(), path)

    def test_fixture_has_visible_and_offscreen_ordinary_instances(self) -> None:
        self.assertEqual(9, len(fixture.VISIBLE_ENTITIES))
        self.assertEqual(42, len(fixture.OFFSCREEN_ENTITIES))
        entity_text = "".join(fixture.INSTANCE_ENTITIES)
        self.assertEqual(51, entity_text.count('"classname" "func_wall"'))
        self.assertIn('"origin" "0 1000 0"', entity_text)
        self.assertIn('"origin" "0 -780 0"', entity_text)

    def test_generator_writes_only_the_culling_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            asset_root = Path(temp) / "assets"
            self.assertEqual(
                {asset_root / "maps" / fixture.MAP_NAME},
                set(fixture.generated_outputs(asset_root)),
            )


if __name__ == "__main__":
    unittest.main()
