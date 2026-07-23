#!/usr/bin/env python3
"""Parser and contract tests for the FR-10-T04 exact-bundle parent."""

from __future__ import annotations

import copy
import importlib.util
import re
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
from types import SimpleNamespace
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = ROOT / "tools" / "networking" / "run_fr10_t04_acceptance.py"
SPEC = importlib.util.spec_from_file_location("fr10_t04_acceptance_under_test", RUNNER_PATH)
assert SPEC is not None and SPEC.loader is not None
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


def native_container(mask: int, client_role: str = "shooter") -> dict[str, Any]:
    return {
        "clients": {
            client_role: {
                "schema": 1, "enabled": 1, "mode": 2,
                "capability_confirmed": 1, "server_active": 1,
                "protocol": 1038, "public_mask": mask,
                "private_mask": mask, "failures": 0, "last_failure": 0,
            },
        },
        "server_peers": [{
            "schema": 1, "enabled": 1, "protocol": 1038,
            "public_mask": mask, "private_mask": mask,
            "wire_committed": 1, "failures": 0, "last_failure": 0,
            "rx_rejections": 0, "tx_ack_rejections": 0, "server_active": 1,
        }],
        "snapshot_peers": [{
            "schema": 1, "sender": 1, "acks": 1, "released": 1,
            "queue_failures": 0, "rejected": 0,
            "retired_sender": 0, "retired_retained": 0,
        }],
    }


def native_base_container(mask: int) -> dict[str, Any]:
    container = native_container(mask)
    del container["snapshot_peers"]
    container["clients"]["target"] = copy.deepcopy(
        container["clients"]["shooter"]
    )
    container["server_peers"].append(copy.deepcopy(container["server_peers"][0]))
    return container


def legacy_capability_container(mask: int = 0x03) -> dict[str, Any]:
    def client(epoch: int) -> dict[str, int]:
        return {
            "schema": 1, "valid": 1, "phase": 2, "protocol": 1038,
            "epoch": epoch, "offered": mask, "supported": mask,
            "peer_supported": mask, "negotiated": mask,
        }

    def server(slot: int, epoch: int) -> dict[str, int]:
        return {
            "schema": 1, "slot": slot, "protocol": 1038, "epoch": epoch,
            "offered": mask, "supported": mask, "negotiated": mask,
            "confirm_sent": 1, "failed": 0, "native_shadow": 0,
            "input_batch_requested": 0, "command_parser": 1,
        }

    return {
        "clients": {"shooter": client(7), "target": client(8)},
        "server_peers": [server(0, 7), server(1, 8)],
    }


def parity() -> dict[str, int]:
    return {
        "matches": 3, "receipts": 3, "passes": 7,
        "conflicts": 0, "mismatches": 0, "resync": 0,
        "unmatched": 0, "outstanding": 0,
    }


def client_command(
    *, event: bool, snapshot: bool, native: bool = True,
    presentation: bool = False,
) -> list[str]:
    command = [
        "client.exe",
        "+set", "win_headless", "1",
        "+set", "cl_headless", "1",
        "+set", "in_enable", "0",
        "+set", "in_grab", "0",
        "+set", "s_enable", "0",
    ]
    if native:
        command.extend(("+set", "cl_worr_native_shadow", "1"))
    if native and event:
        command.extend(("+set", "cl_worr_native_event_shadow", "1"))
    if native and snapshot:
        command.extend(("+set", "cl_worr_native_snapshot_shadow", "1"))
    if presentation:
        command.extend(("+set", "cl_headless", "0"))
    command.extend(("+connect", "127.0.0.1:29440"))
    return command


def dedicated_command(
    *, event: bool, snapshot: bool, native: bool = True,
) -> list[str]:
    command = ["dedicated.exe"]
    if native:
        command.extend(("+set", "sv_worr_native_shadow", "1"))
    if event:
        command.extend(("+set", "sv_worr_native_event_shadow", "1"))
    if snapshot:
        command.extend(("+set", "sv_worr_native_snapshot_shadow", "1"))
    command.extend(("+map", "worr_fr10_rewind_mover"))
    return command


def canonical_report(lane: str) -> dict[str, Any]:
    event = lane in ("event", "combined")
    snapshot = lane in ("snapshot", "combined")
    weapon = {
        "legacy": "blaster-legacy-capability-status",
        "event": "blaster-local-action-lease",
        "snapshot": "blaster-native-snapshot-presentation",
        "combined": "blaster-local-action-lease-combined",
    }[lane]
    run: dict[str, Any] = {
        "status": {"status": "pass", "failure_code": 0},
        "shooter_terminated_by_gate": True,
        "target_terminated_by_gate": True,
        "spectator_terminated_by_gate": False,
        "server_terminated_by_gate": True,
        "reconnect": {
            "required": event,
            "server_admissions": 3 if event else 2,
            "shooter_serverdata_packets": 2 if event else 1,
        },
        "local_action_authority_parity": parity() if event else None,
        "legacy_capability_status": (
            legacy_capability_container() if lane == "legacy" else None
        ),
        "native_event_shadow": (
            native_base_container(0x73) if lane == "event" else None
        ),
        "native_snapshot_shadow": (
            native_container(0x57, "target") if lane == "snapshot" else None
        ),
        "combined_native_shadow": (
            native_container(0x77) if lane == "combined" else None
        ),
        "combined_native_preflight": (
            native_container(0x77) if lane == "combined" else None
        ),
        "native_snapshot_presentation": ({
            "native_authority_samples": 3,
            "promoted_transforms": 3,
            "clock_failures": 0,
            "pair_failures": 0,
            "alignment_failures": 0,
            "sample_failures": 0,
            "event_audit_failures": 0,
            "parity_mismatches": 0,
        } if lane == "snapshot" else None),
    }
    return {
        "schema": RUNNER.CANONICAL_CHILD_SCHEMA,
        "weapon": weapon,
        "repeat": 1,
        "client_count": 2,
        "weapon_policy": 11,
        "expected_damage": 15,
        "status": {"status": "pass", "failure_code": 0},
        "dedicated_command": dedicated_command(
            event=event, snapshot=snapshot, native=lane != "legacy",
        ),
        "shooter_command": client_command(
            event=event, snapshot=snapshot,
            native=lane not in ("legacy", "snapshot"),
        ),
        "target_command": client_command(
            event=event, snapshot=snapshot, native=lane != "legacy",
            presentation=lane == "snapshot",
        ),
        "runs": [run],
    }


def command_report(mask: int = 0x53) -> dict[str, Any]:
    def trial() -> dict[str, Any]:
        status = {
            "schema": 1, "enabled": 1, "protocol": 1038,
            "public_mask": mask, "private_mask": mask,
            "failures": 0, "last_failure": 0,
        }
        return {
            "statuses": {"client": copy.deepcopy(status), "server": status},
            "processes": {
                "client_terminated_by_harness": True,
                "server_terminated_by_harness": True,
            },
            "reliable_delivery": {"complete_exact_once": True},
        }
    return {
        "schema": "worr.networking.native-shadow-runtime.v1",
        "passed": True,
        "trials": {
            "fragment_pressure": trial(),
            "post_burst_async_ack": trial(),
        },
    }


class Fr10T04AcceptanceTests(unittest.TestCase):
    def test_exact_mask_manifest_is_literal_and_complete(self) -> None:
        rows = RUNNER.validate_exact_masks(RUNNER.EXACT_MASKS)
        self.assertEqual(
            [(row["lane"], row["mask"]) for row in rows],
            [
                ("legacy", 0x03), ("command", 0x53),
                ("event", 0x73), ("snapshot", 0x57),
                ("combined", 0x77),
            ],
        )
        mutated = [dict(row) for row in rows]
        mutated[3]["mask"] = 0x77
        with self.assertRaisesRegex(ValueError, "manifest drifted"):
            RUNNER.validate_exact_masks(mutated)

    def test_fixed_manifests_cover_the_required_t04_risks(self) -> None:
        RUNNER.validate_manifests()
        document = RUNNER.manifest_document()
        core = {key: value for key, value in document.items() if key != "sha256"}
        self.assertEqual(document["sha256"], RUNNER.semantic_sha256(core))
        self.assertEqual(len(document["focused"]), 11)
        self.assertEqual(len(document["live"]), 5)
        self.assertTrue({
            "inc/common/net/capability.h",
            "inc/common/net/native_codec.h",
            "inc/common/net/native_demo.h",
            "inc/common/net/native_demo_recorder.h",
            "inc/common/net/native_input_batch.h",
            "inc/common/net/native_input_batch_sideband.h",
            "inc/common/net/native_input_delivery.h",
            "inc/common/net/native_event_sender.h",
            "inc/common/net/predicted_presentation.h",
            "inc/common/net/event_journal.h",
            "inc/common/protocol.h",
            "inc/client/native_demo_recorder.h",
            "inc/client/native_readiness_pilot.h",
            "inc/server/native_shadow.h",
            "inc/shared/cgame_prediction.h",
            "inc/shared/cgame_event_runtime.h",
            "inc/shared/event_abi.h",
            "src/common/net/capability.c",
            "src/common/net/native_codec.c",
            "src/common/net/native_demo.c",
            "src/common/net/native_demo_recorder.c",
            "src/common/net/native_input_batch.c",
            "src/common/net/native_input_batch_sideband.c",
            "src/common/net/native_input_delivery.c",
            "src/common/net/native_event_sender.c",
            "src/common/net/event_abi.c",
            "src/common/net/predicted_presentation.c",
            "src/common/net/event_journal.c",
            "src/client/demo.cpp",
            "src/client/cgame_prediction_input.cpp",
            "src/client/input.cpp",
            "src/client/native_demo_recorder.cpp",
            "src/client/net_capability.cpp",
            "src/client/native_readiness_pilot.cpp",
            "src/game/cgame/cg_main.cpp",
            "src/game/cgame/cg_entities.cpp",
            "src/game/cgame/cg_event_runtime.hpp",
            "src/game/cgame/cg_event_runtime.cpp",
            "src/game/cgame/cg_local_interaction.hpp",
            "src/game/cgame/cg_local_interaction.cpp",
            "src/game/cgame/cg_canonical_render_entities.hpp",
            "src/game/cgame/cg_canonical_render_entities.cpp",
            "src/game/cgame/cg_native_event_presenter.hpp",
            "src/game/cgame/cg_native_event_presenter.cpp",
            "src/server/commands.c",
            "src/server/main.c",
            "src/server/native_shadow.c",
            "src/server/user.c",
            "tools/networking/cgame_canonical_render_entities_test.cpp",
            "tools/networking/cgame_native_event_presenter_test.cpp",
            "tools/networking/cgame_event_runtime_test.cpp",
            "tools/networking/cgame_local_interaction_test.cpp",
            "tools/networking/native_demo_recorder_test.c",
            "tools/networking/native_input_batch_test.c",
            "tools/networking/native_input_delivery_test.c",
            "tools/networking/native_event_sender_test.c",
            "tools/networking/predicted_presentation_test.c",
            "tools/networking/test_canonical_snapshot_render_policy_contract.py",
            "tools/networking/test_run_native_shadow_runtime_smoke.py",
            "tools/networking/test_run_canonical_rail_damage_runtime_gate.py",
        } <= set(RUNNER.SOURCE_INPUTS))
        canonical_source = (
            ROOT / "tools" / "networking" /
            "run_canonical_rail_damage_runtime_gate.py"
        ).read_text(encoding="utf-8")
        child_schema = re.search(
            r'^SCHEMA = "([^"]+)"$', canonical_source, re.MULTILINE
        )
        self.assertIsNotNone(child_schema)
        assert child_schema is not None
        self.assertEqual(child_schema.group(1), RUNNER.CANONICAL_CHILD_SCHEMA)

    def test_strict_json_rejects_duplicates_nonfinite_and_nonobject(self) -> None:
        self.assertEqual(RUNNER.strict_json(b'{"ok":1}', "test"), {"ok": 1})
        for data in (b'{"a":1,"a":2}', b'{"a":NaN}', b'[1,2]'):
            with self.subTest(data=data):
                with self.assertRaises(ValueError):
                    RUNNER.strict_json(data, "test")

    def test_output_is_confined_to_tmp_networking_json(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            expected = (root / ".tmp" / "networking" / "evidence.json").resolve()
            self.assertEqual(
                RUNNER.require_networking_output(
                    root, Path(".tmp/networking/evidence.json")
                ),
                expected,
            )
            for invalid in (Path("evidence.json"), Path(".tmp/networking/evidence.txt")):
                with self.subTest(path=invalid):
                    with self.assertRaises(ValueError):
                        RUNNER.require_networking_output(root, invalid)

    def test_runtime_closure_includes_every_loaded_live_component(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            install = Path(temporary).resolve() / ".install"
            base = install / "basew"
            maps = base / "maps"
            maps.mkdir(parents=True)
            client = install / "worr_x86_64.exe"
            dedicated = install / "worr_ded_x86_64.exe"
            required = (
                client, dedicated,
                install / "worr_engine_x86_64.dll",
                install / "worr_ded_engine_x86_64.dll",
                install / "worr_opengl_x86_64.dll",
                install / "rmlui_core.dll",
                base / "cgame_x86_64.dll",
                base / "sgame_x86_64.dll",
                base / "pak0.pkz",
                base / "config.cfg",
                maps / "worr_fr10_rewind_mover.bsp",
            )
            for index, path in enumerate(required):
                path.write_bytes(f"artifact-{index}".encode())
            paths = RUNNER.runtime_artifact_paths(client, dedicated, install)
            self.assertEqual(
                set(paths),
                {
                    "client_launcher", "dedicated_launcher", "client_engine",
                    "dedicated_engine", "renderer", "rmlui_core", "cgame",
                    "sgame", "pak", "config", "canonical_fixture",
                },
            )

    def test_runtime_clone_excludes_preserved_transient_trees(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            install = root / ".install"
            base = install / "basew"
            maps = base / "maps"
            maps.mkdir(parents=True)
            client = install / "worr_x86_64.exe"
            dedicated = install / "worr_ded_x86_64.exe"
            required = (
                client, dedicated,
                install / "worr_engine_x86_64.dll",
                install / "worr_ded_engine_x86_64.dll",
                install / "worr_opengl_x86_64.dll",
                install / "rmlui_core.dll",
                base / "cgame_x86_64.dll",
                base / "sgame_x86_64.dll",
                base / "pak0.pkz",
                base / "config.cfg",
                maps / "worr_fr10_rewind_mover.bsp",
            )
            for index, path in enumerate(required):
                path.write_bytes(f"artifact-{index}".encode())
            for transient in (install / ".install", install / ".tmp"):
                transient.mkdir()
                (transient / "stale.bin").write_bytes(b"stale")
            (install / "crashdump_1.dmp").write_bytes(b"stale")

            destination = root / "isolated"
            clone_paths, source_hashes = RUNNER.clone_runtime(
                install, destination, client, dedicated
            )

            self.assertEqual(set(clone_paths), set(source_hashes))
            self.assertFalse((destination / ".install").exists())
            self.assertFalse((destination / ".tmp").exists())
            self.assertFalse((destination / "crashdump_1.dmp").exists())
            self.assertTrue((destination / "basew" / "pak0.pkz").is_file())

    def test_focused_output_requires_markers_and_expected_stderr_policy(self) -> None:
        normal = RUNNER.FOCUSED_MANIFEST[0]
        RUNNER.validate_focused_output(normal, "capability_test: ok\n", "")
        with self.assertRaises(ValueError):
            RUNNER.validate_focused_output(normal, "wrong\n", "")
        with self.assertRaises(ValueError):
            RUNNER.validate_focused_output(normal, "capability_test: ok\n", "noise")
        snapshot = RUNNER.FOCUSED_MANIFEST[-1]
        RUNNER.validate_focused_output(
            snapshot,
            "native_snapshot_production_virtual_link_test: ok\n",
            "cg_prediction_snapshot_authority: mode=2\n",
        )

    def test_live_commands_pin_runner_mode_mask_and_headless_children(self) -> None:
        args = SimpleNamespace(
            native_command_timeout=120.0,
            canonical_timeout=45.0,
            base_port=29440,
        )
        event = next(
            row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "event"
        )
        command = RUNNER.build_live_command(
            event, Path("child.py"), Path("client.exe"),
            Path("dedicated.exe"), Path("install"), Path("report.json"), args,
        )
        self.assertEqual(command[:2], [sys.executable, "child.py"])
        self.assertEqual(command[command.index("--repeat") + 1], "1")
        self.assertEqual(command[command.index("--port") + 1], "29440")
        self.assertEqual(
            command[command.index("--weapon") + 1],
            "blaster-local-action-lease",
        )
        legacy = RUNNER.build_live_command(
            next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "legacy"),
            Path("child.py"), Path("client.exe"), Path("dedicated.exe"),
            Path("install"), Path("legacy.json"), args,
        )
        self.assertEqual(
            legacy[legacy.index("--weapon") + 1],
            "blaster-legacy-capability-status",
        )
        self.assertEqual(legacy[legacy.index("--port") + 1], "29443")
        native = RUNNER.build_live_command(
            next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "command"),
            Path("native.py"), Path("client.exe"),
            Path("dedicated.exe"), Path("install"), Path("report.json"), args,
        )
        self.assertNotIn("--weapon", native)
        self.assertEqual(native[native.index("--timeout") + 1], "120.0")

    def test_command_report_requires_exact_public_and_private_0x53(self) -> None:
        summary = RUNNER.validate_command_report(command_report(), 0x53)
        self.assertEqual(summary["mask_observation"], "direct-client-and-server-status")
        bad = command_report()
        bad["trials"]["fragment_pressure"]["statuses"]["server"]["public_mask"] = 0x03
        with self.assertRaises(ValueError):
            RUNNER.validate_command_report(bad, 0x53)

    def test_canonical_live_reports_prove_exact_legacy_and_native_scope(self) -> None:
        rows = {row["lane"]: row for row in RUNNER.LIVE_MANIFEST}
        for lane in ("legacy", "event", "snapshot", "combined"):
            with self.subTest(lane=lane):
                summary = RUNNER.validate_canonical_report(
                    canonical_report(lane), rows[lane]
                )
                self.assertEqual(summary["lane"], lane)
                if lane in ("legacy", "event"):
                    self.assertTrue(summary["numeric_status_in_child_report"])
                    self.assertEqual(
                        summary["mask_observation"],
                        "direct-client-and-server-status",
                    )
                    peer_field = (
                        "legacy_peers" if lane == "legacy" else "native_peers"
                    )
                    self.assertGreaterEqual(summary[peer_field], 1)
                else:
                    self.assertEqual(
                        summary["mask_observation"],
                        "direct-client-and-server-status",
                    )
                    self.assertGreaterEqual(summary["snapshot_peers"], 1)

    def test_canonical_report_rejects_mask_headless_and_reconnect_drift(self) -> None:
        legacy = canonical_report("legacy")
        legacy["runs"][0]["legacy_capability_status"]["clients"]["shooter"][
            "negotiated"
        ] = 0x53
        with self.assertRaises(ValueError):
            RUNNER.validate_canonical_report(
                legacy,
                next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "legacy"),
            )

        snapshot = canonical_report("snapshot")
        snapshot["runs"][0]["native_snapshot_shadow"]["clients"]["target"][
            "public_mask"
        ] = 0x03
        with self.assertRaises(ValueError):
            RUNNER.validate_canonical_report(
                snapshot,
                next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "snapshot"),
            )

        snapshot = canonical_report("snapshot")
        snapshot["shooter_command"].extend((
            "+set", "cl_worr_native_shadow", "1",
            "+set", "cl_worr_native_snapshot_shadow", "1",
        ))
        with self.assertRaises(ValueError):
            RUNNER.validate_canonical_report(
                snapshot,
                next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "snapshot"),
            )

        event = canonical_report("event")
        command = event["shooter_command"]
        command[command.index("win_headless") + 1] = "0"
        with self.assertRaises(ValueError):
            RUNNER.validate_canonical_report(
                event,
                next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "event"),
            )

        event = canonical_report("event")
        event["runs"][0]["native_event_shadow"]["server_peers"][0][
            "private_mask"
        ] = 0x53
        with self.assertRaises(ValueError):
            RUNNER.validate_canonical_report(
                event,
                next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "event"),
            )

        combined = canonical_report("combined")
        combined["runs"][0]["reconnect"]["required"] = False
        with self.assertRaises(ValueError):
            RUNNER.validate_canonical_report(
                combined,
                next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "combined"),
            )

        combined = canonical_report("combined")
        combined["runs"][0]["combined_native_preflight"] = None
        with self.assertRaises(ValueError):
            RUNNER.validate_canonical_report(
                combined,
                next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "combined"),
            )

        combined = canonical_report("combined")
        combined["runs"][0]["combined_native_shadow"]["snapshot_peers"][0][
            "released"
        ] = 0
        with self.assertRaises(ValueError):
            RUNNER.validate_canonical_report(
                combined,
                next(row for row in RUNNER.LIVE_MANIFEST if row["lane"] == "combined"),
            )

    def test_parent_evidence_remains_partial_after_all_bounded_rows_pass(self) -> None:
        focused = [
            {
                "name": row["name"],
                "binary": {"sha256": str(index) * 64},
                "process": {"argv_sha256": "a" * 64},
                "stdout": {"sha256": "b" * 64},
                "stderr": {"sha256": "c" * 64},
            }
            for index, row in enumerate(RUNNER.FOCUSED_MANIFEST, start=1)
        ]
        live = [
            {
                "lane": row["lane"], "mask": row["mask"],
                "mask_observation": "direct-client-and-server-status",
                "runner": {"sha256": "d" * 64},
                "process": {"argv_sha256": "e" * 64},
                "report": {"sha256": str(index) * 64},
                "stdout": {"sha256": "f" * 64},
                "stderr": {"sha256": "0" * 64},
            }
            for index, row in enumerate(RUNNER.LIVE_MANIFEST, start=1)
        ]
        evidence = RUNNER.build_evidence(
            run_id="test", started=datetime.now(timezone.utc), scope="full",
            manifest=RUNNER.manifest_document(),
            sources={"source": {"sha256": "a" * 64}},
            focused=focused, live=live,
            q2proto={"sha256": "b" * 64},
            runtime={
                "isolated": True,
                "components": {"client": {"sha256": "1" * 64}},
            },
        )
        self.assertEqual(evidence["status"], "partial")
        self.assertTrue(evidence["gates"]["all_live_children_passed"])
        self.assertTrue(evidence["gates"]["direct_numeric_legacy_status"])
        self.assertTrue(evidence["gates"]["direct_numeric_event_status"])
        self.assertFalse(evidence["gates"]["task_complete"])
        self.assertIn("legacy", " ".join(evidence["limitations"]).lower())

        missing_direct_event = copy.deepcopy(live)
        event_index = next(
            index for index, row in enumerate(missing_direct_event)
            if row["lane"] == "event"
        )
        missing_direct_event[event_index]["mask_observation"] = (
            "fixed-config-plus-native-receipt"
        )
        with self.assertRaisesRegex(ValueError, "direct client/server numeric"):
            RUNNER.build_evidence(
                run_id="test", started=datetime.now(timezone.utc), scope="full",
                manifest=RUNNER.manifest_document(),
                sources={"source": {"sha256": "a" * 64}},
                focused=focused, live=missing_direct_event,
                q2proto={"sha256": "b" * 64},
                runtime={
                    "isolated": True,
                    "components": {"client": {"sha256": "1" * 64}},
                },
            )

        missing_direct_legacy = copy.deepcopy(live)
        legacy_index = next(
            index for index, row in enumerate(missing_direct_legacy)
            if row["lane"] == "legacy"
        )
        missing_direct_legacy[legacy_index]["mask_observation"] = "launch-config-only"
        with self.assertRaisesRegex(ValueError, "direct client/server numeric"):
            RUNNER.build_evidence(
                run_id="test", started=datetime.now(timezone.utc), scope="full",
                manifest=RUNNER.manifest_document(),
                sources={"source": {"sha256": "a" * 64}},
                focused=focused, live=missing_direct_legacy,
                q2proto={"sha256": "b" * 64},
                runtime={
                    "isolated": True,
                    "components": {"client": {"sha256": "1" * 64}},
                },
            )

        changed_live = copy.deepcopy(live)
        changed_live[0]["stdout"]["sha256"] = "9" * 64
        changed = RUNNER.build_evidence(
            run_id="test", started=datetime.now(timezone.utc), scope="full",
            manifest=RUNNER.manifest_document(),
            sources={"source": {"sha256": "a" * 64}},
            focused=focused, live=changed_live,
            q2proto={"sha256": "b" * 64},
            runtime={
                "isolated": True,
                "components": {"client": {"sha256": "1" * 64}},
            },
        )
        self.assertNotEqual(
            changed["semantic_sha256"], evidence["semantic_sha256"]
        )


if __name__ == "__main__":
    unittest.main()
