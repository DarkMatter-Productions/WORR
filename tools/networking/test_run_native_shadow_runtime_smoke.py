#!/usr/bin/env python3
"""Regression tests for the two-process native-shadow acceptance parser."""

from __future__ import annotations

import copy
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).with_name("run_native_shadow_runtime_smoke.py")
SPEC = importlib.util.spec_from_file_location("native_shadow_smoke", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
SMOKE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SMOKE
SPEC.loader.exec_module(SMOKE)


def client_status(**overrides: int) -> dict[str, int]:
    values = {
        "schema": 1,
        "enabled": 1,
        "mode": 2,
        "hooks": 1,
        "capability_confirmed": 1,
        "readiness_phase": 5,
        "official_epoch": 41,
        "transport_epoch": 73,
        "protocol": SMOKE.PROTOCOL,
        "public_mask": SMOKE.PUBLIC_MASK,
        "private_mask": SMOKE.PRIVATE_MASK,
        "probe_hold": 1,
        "cancelled_through_epoch": 0,
        "cancellation_barriers": 1,
        "cancelled_transports": 0,
        "cancelled_command_tx": 0,
        "cancelled_event_rx": 0,
        "cancelled_event_receipts": 0,
        "stale_cancelled_carriers": 0,
        "stale_cancelled_readiness_records": 0,
        "challenges": 1,
        "client_ready_queued": 1,
        "server_active": 1,
        "proof_enqueued": 1,
        "retained": 0,
        "retained_highwater": 1,
        "retained_releases": 1,
        "tx_first_sends": 1,
        "tx_retries": 2,
        "tx_handoffs": 3,
        "ack_carriers": 3,
        "acknowledged_reliable": 1,
        "drains": 0,
        "failures": 0,
        "last_failure": 0,
    }
    values.update(overrides)
    return values


def server_status(**overrides: int) -> dict[str, int]:
    values = {
        "schema": 1,
        "slot": 0,
        "protocol": SMOKE.PROTOCOL,
        "enabled": 1,
        "lifecycle": 2,
        "hooks": 1,
        "readiness_phase": 4,
        "official_epoch": 41,
        "transport_epoch": 73,
        "cancelled_through_epoch": 0,
        "public_mask": SMOKE.PUBLIC_MASK,
        "private_mask": SMOKE.PRIVATE_MASK,
        "wire_committed": 1,
        "wire_committed_transport_epoch": 73,
        "challenges_queued": 1,
        "client_ready": 1,
        "server_active": 1,
        "rx_carriers": 3,
        "rx_commits": 1,
        "rx_repeat_refreshes": 2,
        "legacy_joins": 1,
        "command_matches": 1,
        "command_mismatches": 0,
        "sample_mismatches": 0,
        "ack_eligible": 0,
        "ack_prepares": 3,
        "ack_handoffs": 3,
        "async_rate_deferrals": 4,
        "async_fragment_deferrals": 5,
        "async_wake_attempts": 3,
        "async_ack_handoffs": 3,
        "async_wake_no_handoff": 0,
        "rx_rejections": 0,
        "tx_ack_rejections": 0,
        "rx_drained": 0,
        "drains": 0,
        "failures": 0,
        "cancellation_barriers": 1,
        "cancelled_transports": 0,
        "cancelled_rx_messages": 0,
        "cancelled_receipts": 0,
        "cancelled_event_records": 0,
        "stale_cancelled_carriers": 0,
        "stale_cancelled_readiness_records": 0,
        "last_failure": 0,
    }
    values.update(overrides)
    return values


def status_row(prefix: str, values: dict[str, int]) -> str:
    tokens: list[str] = []
    for name, value in values.items():
        if name in ("public_mask", "private_mask"):
            tokens.append(f"{name}=0x{value:02x}")
        else:
            tokens.append(f"{name}={value}")
    return prefix + " " + " ".join(tokens)


def impairment_rows(seed: int, **counter_overrides: int) -> str:
    counters = {
        "seen": 200,
        "dropped": 0,
        "burst_dropped": 0,
        "reordered": 0,
        "duplicated": 0,
        "corrupted": 0,
        "upstream_stalled": 0,
        "throttled": 0,
        "overflow": 0,
        "resets": 0,
    }
    counters.update(counter_overrides)
    return (
        f"net_impair: enabled=1 seed={seed} latency=25 jitter=0 "
        "loss=0.00 burst=0.00/3 reorder=0.00 duplicate=0.00 "
        "corrupt=0.00 upstream_stall=0 rate_kbps=0 "
        "queue=1/1024 high_water=9\n"
        "net_impair counters: "
        + " ".join(f"{name}={value}" for name, value in counters.items())
    )


def runtime_texts(
    *,
    port: int = 28017,
    client_marker: str = "client_complete",
    server_marker: str = "server_complete",
    reliable_record_count: int = SMOKE.FRAGMENT_RELIABLE_RECORD_COUNT,
) -> tuple[str, str]:
    client = (
        f"Connected to {SMOKE.ADDRESS}:{port} (protocol {SMOKE.PROTOCOL}).\n"
        f"Serverdata packet received (protocol={SMOKE.PROTOCOL})\n"
        + "\n".join(SMOKE.reliable_payloads(reliable_record_count))
        + "\n"
        + status_row(SMOKE.CLIENT_STATUS_PREFIX, client_status())
        + "\n"
        + impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        + "\n"
        + client_marker
        + "\n"
    )
    server = (
        f"SpawnServer: {SMOKE.MAP_NAME}\n"
        "Going from cs_primed to cs_spawned for native_shadow_probe\n"
        + status_row(SMOKE.SERVER_STATUS_PREFIX, server_status())
        + "\n"
        + impairment_rows(SMOKE.SERVER_IMPAIR_SEED)
        + "\n"
        + server_marker
        + "\n"
    )
    return client, server


class _RunningProcess:
    returncode = None

    @staticmethod
    def poll() -> None:
        return None


class _FakeClock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class NativeShadowProcessTimingTests(unittest.TestCase):
    def _run(
        self,
        *,
        clock: _FakeClock,
        timeout: float,
        terminate_side_effect: object,
    ) -> dict[str, object]:
        client_marker = "client_complete"
        server_marker = "server_complete"

        def read_text(path: Path) -> str:
            if path.name == "client.stdout.log":
                return client_marker
            if path.name == "server.stdout.log":
                return server_marker
            return ""

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (
                mock.patch.object(
                    SMOKE,
                    "start_headless_process",
                    side_effect=[_RunningProcess(), _RunningProcess()],
                ),
                mock.patch.object(SMOKE, "_wait_for_server_map"),
                mock.patch.object(SMOKE, "_read_text", side_effect=read_text),
                mock.patch.object(
                    SMOKE,
                    "terminate_process",
                    side_effect=terminate_side_effect,
                ),
                mock.patch.object(
                    SMOKE.time, "monotonic", side_effect=clock.monotonic
                ),
                mock.patch.object(SMOKE.time, "sleep", side_effect=clock.sleep),
            ):
                return SMOKE.run_processes(
                    server_argv=("server",),
                    client_argv=("client",),
                    working_dir=root,
                    run_root=root,
                    client_marker=client_marker,
                    server_marker=server_marker,
                    timeout=timeout,
                )

    def test_slow_teardown_does_not_invalidate_timely_evidence(self) -> None:
        clock = _FakeClock()

        def slow_terminate(_process: object) -> bool:
            clock.now += 10.0
            return True

        result = self._run(
            clock=clock,
            timeout=1.0,
            terminate_side_effect=slow_terminate,
        )

        self.assertEqual(result["elapsed_seconds"], 0.25)
        self.assertEqual(clock.now, 20.25)

    def test_evidence_completed_after_live_grace_deadline_fails(self) -> None:
        clock = _FakeClock()

        with self.assertRaisesRegex(
            RuntimeError, "timed out during the post-evidence live grace"
        ):
            self._run(
                clock=clock,
                timeout=0.2,
                terminate_side_effect=lambda _process: True,
            )


def replace_status(text: str, prefix: str, values: dict[str, int]) -> str:
    lines = text.splitlines()
    replacement = status_row(prefix, values)
    return "\n".join(replacement if prefix in line else line for line in lines) + "\n"


def valid_profile() -> dict[str, object]:
    return SMOKE.build_profile({
        SMOKE.FRAGMENT_TRIAL: 28017,
        SMOKE.ASYNC_TRIAL: 28018,
    })


class NativeShadowStatusParserTests(unittest.TestCase):
    def test_complete_runtime_evidence_passes(self) -> None:
        client, server = runtime_texts()
        evidence = SMOKE.validate_runtime_text(
            client,
            server,
            client_marker="client_complete",
            server_marker="server_complete",
            port=28017,
        )
        self.assertEqual(evidence["client_status"]["private_mask"], 0x53)
        self.assertEqual(
            evidence["server_status"]["async_ack_handoffs"], 3
        )
        self.assertTrue(
            evidence["reliable_delivery"]["complete_exact_once"]
        )

    def test_status_parser_rejects_missing_and_duplicate_rows(self) -> None:
        client, _ = runtime_texts()
        with self.assertRaisesRegex(RuntimeError, "missing WORR_NATIVE_SERVER"):
            SMOKE.parse_server_status(client)
        duplicate = client + status_row(
            SMOKE.CLIENT_STATUS_PREFIX, client_status()
        )
        with self.assertRaisesRegex(RuntimeError, "duplicate WORR_NATIVE_CLIENT"):
            SMOKE.parse_client_status(duplicate)

    def test_status_parser_rejects_missing_duplicate_and_malformed_fields(self) -> None:
        values = client_status()
        del values["acknowledged_reliable"]
        with self.assertRaisesRegex(RuntimeError, "missing fields"):
            SMOKE.parse_client_status(
                status_row(SMOKE.CLIENT_STATUS_PREFIX, values)
            )
        row = status_row(SMOKE.CLIENT_STATUS_PREFIX, client_status())
        with self.assertRaisesRegex(RuntimeError, "duplicate or empty"):
            SMOKE.parse_client_status(row + " mode=2")
        with self.assertRaisesRegex(RuntimeError, "non-numeric"):
            SMOKE.parse_client_status(row.replace("mode=2", "mode=active"))

    def test_wrong_protocol_or_masks_are_rejected_on_both_endpoints(self) -> None:
        for endpoint, validator, base in (
            ("client", SMOKE.validate_client_status, client_status),
            ("server", SMOKE.validate_server_status, server_status),
        ):
            for field, value in (
                ("protocol", 36),
                ("public_mask", 0x13),
                ("private_mask", 0x03),
            ):
                with self.subTest(endpoint=endpoint, field=field):
                    with self.assertRaisesRegex(RuntimeError, field):
                        validator(base(**{field: value}))

    def test_readiness_chain_and_exact_command_counts_are_required(self) -> None:
        client_zero = (
            "challenges",
            "client_ready_queued",
            "server_active",
            "proof_enqueued",
            "tx_first_sends",
            "tx_handoffs",
            "ack_carriers",
            "acknowledged_reliable",
        )
        for field in client_zero:
            with self.subTest(endpoint="client", field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_client_status(client_status(**{field: 0}))
        server_zero = (
            "challenges_queued",
            "client_ready",
            "server_active",
            "rx_carriers",
            "rx_commits",
            "legacy_joins",
            "command_matches",
        )
        for field in server_zero:
            with self.subTest(endpoint="server", field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_server_status(server_status(**{field: 0}))
        with self.assertRaisesRegex(RuntimeError, "rx_commits"):
            SMOKE.validate_server_status(server_status(rx_commits=2))
        with self.assertRaisesRegex(RuntimeError, "command_matches"):
            SMOKE.validate_server_status(server_status(command_matches=2))
        with self.assertRaisesRegex(RuntimeError, "wire-committed epoch"):
            SMOKE.validate_server_status(
                server_status(wire_committed_transport_epoch=72)
            )

    def test_each_trial_requires_only_its_scheduler_evidence(self) -> None:
        for spec in SMOKE.TRIAL_SPECS:
            for field in spec.required_server_evidence:
                with self.subTest(trial=spec.name, field=field):
                    with self.assertRaisesRegex(RuntimeError, field):
                        SMOKE.validate_server_status(
                            server_status(**{field: 0}),
                            required_evidence=spec.required_server_evidence,
                        )
        SMOKE.validate_server_status(
            server_status(
                async_wake_attempts=0,
                async_ack_handoffs=0,
            ),
            required_evidence=SMOKE.TRIAL_SPECS[0].required_server_evidence,
        )
        SMOKE.validate_server_status(
            server_status(async_fragment_deferrals=0),
            required_evidence=SMOKE.TRIAL_SPECS[1].required_server_evidence,
        )

    def test_ack_handoff_must_be_classified_as_asynchronous(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "async_ack_handoffs"):
            SMOKE.validate_server_status(
                server_status(async_ack_handoffs=0, ack_handoffs=3),
                required_evidence=("async_ack_handoffs",),
            )
        with self.assertRaisesRegex(RuntimeError, "exceed all handoffs"):
            SMOKE.validate_server_status(
                server_status(async_ack_handoffs=4, ack_handoffs=3,
                              async_wake_attempts=4, ack_prepares=4)
            )

    def test_client_must_prove_retained_transition_and_ack_release(self) -> None:
        for field, value in (
            ("retained", 1),
            ("retained_highwater", 0),
            ("retained_releases", 0),
            ("acknowledged_reliable", 0),
        ):
            with self.subTest(field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_client_status(client_status(**{field: value}))

    def test_nonzero_mismatch_rejection_drain_and_failure_are_rejected(self) -> None:
        for field in (
            "command_mismatches",
            "sample_mismatches",
            "rx_rejections",
            "tx_ack_rejections",
            "rx_drained",
            "drains",
            "failures",
            "cancelled_through_epoch",
            "cancelled_transports",
            "cancelled_rx_messages",
            "cancelled_receipts",
            "cancelled_event_records",
            "stale_cancelled_carriers",
            "stale_cancelled_readiness_records",
            "last_failure",
        ):
            with self.subTest(endpoint="server", field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_server_status(server_status(**{field: 1}))
        for field in (
            "drains",
            "failures",
            "cancelled_through_epoch",
            "cancelled_transports",
            "cancelled_command_tx",
            "cancelled_event_rx",
            "cancelled_event_receipts",
            "stale_cancelled_carriers",
            "stale_cancelled_readiness_records",
            "last_failure",
        ):
            with self.subTest(endpoint="client", field=field):
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_client_status(client_status(**{field: 1}))

        for endpoint, validator, status in (
            ("client", SMOKE.validate_client_status, client_status),
            ("server", SMOKE.validate_server_status, server_status),
        ):
            for barrier_count in (0, 2):
                with self.subTest(
                    endpoint=endpoint, cancellation_barriers=barrier_count
                ):
                    with self.assertRaisesRegex(
                        RuntimeError, "cancellation_barriers"
                    ):
                        validator(
                            status(cancellation_barriers=barrier_count)
                        )

    def test_status_pair_rejects_epoch_disagreement(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "transport_epoch"):
            SMOKE.validate_status_pair(
                client_status(),
                server_status(
                    transport_epoch=74,
                    wire_committed_transport_epoch=74,
                ),
            )

    def test_terminal_order_rejects_missing_duplicate_and_late_status(self) -> None:
        status = status_row(SMOKE.CLIENT_STATUS_PREFIX, client_status())
        with self.assertRaisesRegex(RuntimeError, "count is 0"):
            SMOKE.validate_terminal_order(
                status, status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                completion_marker="done"
            )
        with self.assertRaisesRegex(RuntimeError, "count is 2"):
            SMOKE.validate_terminal_order(
                status + "\ndone\ndone\n",
                status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                completion_marker="done",
            )
        with self.assertRaisesRegex(RuntimeError, "did not precede"):
            SMOKE.validate_terminal_order(
                "done\n" + status,
                status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                completion_marker="done",
            )

    def test_terminal_order_rejects_failure_before_or_after_completion(self) -> None:
        status = status_row(SMOKE.CLIENT_STATUS_PREFIX, client_status())
        for text, relation in (
            ("was dropped:\n" + status + "\ndone\n", "before"),
            (status + "\ndone\nServer disconnected\n", "after"),
        ):
            with self.subTest(relation=relation):
                with self.assertRaisesRegex(RuntimeError, relation):
                    SMOKE.validate_terminal_order(
                        text,
                        status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                        completion_marker="done",
                    )
        with self.assertRaisesRegex(RuntimeError, "Cbuf_AddText"):
            SMOKE.validate_terminal_order(
                status + "\nCbuf_AddText: overflow\ndone\n",
                status_prefix=SMOKE.CLIENT_STATUS_PREFIX,
                completion_marker="done",
            )

    def test_reliable_delivery_rejects_missing_and_duplicate_records(self) -> None:
        payloads = SMOKE.reliable_payloads()
        valid = "\n".join(payloads) + "\n"
        evidence = SMOKE.validate_reliable_delivery(valid)
        self.assertEqual(evidence["record_count"], len(payloads))
        with self.assertRaisesRegex(RuntimeError, "PRINT_07"):
            SMOKE.validate_reliable_delivery(
                "\n".join(payloads[:7] + payloads[8:]) + "\n"
            )
        with self.assertRaisesRegex(RuntimeError, "prefix_count=2"):
            SMOKE.validate_reliable_delivery(valid + payloads[3] + "\n")


class NativeShadowImpairmentParserTests(unittest.TestCase):
    def test_latency_only_profile_and_counters_pass(self) -> None:
        config, counters = SMOKE.parse_impairment_status(
            impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        )
        SMOKE.validate_impairment_status(
            config, counters, expected_seed=SMOKE.CLIENT_IMPAIR_SEED
        )

    def test_impairment_parser_rejects_missing_and_duplicate_rows(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "missing net_impair profile"):
            SMOKE.parse_impairment_status("unrelated")
        rows = impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        with self.assertRaisesRegex(RuntimeError, "duplicate net_impair profile"):
            SMOKE.parse_impairment_status(rows + "\n" + rows)

    def test_wrong_impairment_profile_fields_are_rejected(self) -> None:
        config, counters = SMOKE.parse_impairment_status(
            impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        )
        for field, value in (
            ("enabled", 0),
            ("seed", 1),
            ("latency", 0),
            ("loss", 1.0),
            ("rate_kbps", 1),
            ("queue_limit", 7),
            ("high_water", 0),
        ):
            with self.subTest(field=field):
                changed = dict(config)
                changed[field] = value
                with self.assertRaisesRegex(RuntimeError, field.replace("high_water", "latency queue")):
                    SMOKE.validate_impairment_status(
                        changed, counters,
                        expected_seed=SMOKE.CLIENT_IMPAIR_SEED,
                    )

    def test_nonzero_impairment_failures_are_rejected(self) -> None:
        config, counters = SMOKE.parse_impairment_status(
            impairment_rows(SMOKE.CLIENT_IMPAIR_SEED)
        )
        for field in (
            "dropped",
            "burst_dropped",
            "reordered",
            "duplicated",
            "corrupted",
            "upstream_stalled",
            "throttled",
            "overflow",
            "resets",
        ):
            with self.subTest(field=field):
                changed = dict(counters)
                changed[field] = 1
                with self.assertRaisesRegex(RuntimeError, field):
                    SMOKE.validate_impairment_status(
                        config, changed,
                        expected_seed=SMOKE.CLIENT_IMPAIR_SEED,
                    )
        changed = dict(counters)
        changed["seen"] = 0
        with self.assertRaisesRegex(RuntimeError, "no packets"):
            SMOKE.validate_impairment_status(
                config, changed, expected_seed=SMOKE.CLIENT_IMPAIR_SEED
            )


class NativeShadowCommandTests(unittest.TestCase):
    def test_waits_are_chunked_at_the_engine_limit(self) -> None:
        self.assertEqual(
            SMOKE.wait_commands(2501),
            ["+wait", "1000", "+wait", "1000", "+wait", "501"],
        )

    def test_reliable_burst_is_valid_bounded_print_traffic(self) -> None:
        payloads = SMOKE.reliable_payloads()
        self.assertEqual(len(payloads), 16)
        self.assertEqual(sum(map(len, payloads)), 12800)
        self.assertEqual(len(set(payloads)), len(payloads))
        for payload in payloads:
            self.assertEqual(len(payload.encode("ascii")), 800)
            self.assertTrue(
                payload.startswith("WORR_NATIVE_RELIABLE_PRINT_")
            )
            self.assertNotIn('"', payload)

    def test_server_command_pins_real_rate_and_fragment_profile(self) -> None:
        command = SMOKE.server_command(
            Path("server.exe"),
            port=28017,
            client_completion_marker="client_done",
            server_completion_marker="server_done",
        )
        settings = {
            command[index + 1]: command[index + 2]
            for index, value in enumerate(command[:-2])
            if value == "+set"
        }
        self.assertEqual(settings["net_port"], "28017")
        self.assertEqual(settings["net_maxmsglen"], "512")
        self.assertEqual(settings["deathmatch"], "0")
        self.assertEqual(settings["coop"], "0")
        self.assertEqual(settings["maxclients"], "1")
        self.assertEqual(settings["g_owner_auto_join"], "1")
        self.assertEqual(settings["match_auto_join"], "1")
        self.assertEqual(settings["match_force_join"], "1")
        self.assertEqual(
            settings["sv_min_rate"],
            str(SMOKE.FRAGMENT_RATE_BYTES_PER_SECOND),
        )
        self.assertEqual(
            settings["sv_max_rate"],
            str(SMOKE.FRAGMENT_RATE_BYTES_PER_SECOND),
        )
        self.assertEqual(settings["sv_lan_force_rate"], "0")
        self.assertEqual(settings["sv_fps"], str(SMOKE.SERVER_FPS))
        self.assertEqual(settings["sv_worr_native_shadow"], "1")
        printed = [
            command[index + 1]
            for index, value in enumerate(command[:-1])
            if value == "+printall"
        ]
        self.assertEqual(
            len(printed), SMOKE.FRAGMENT_RELIABLE_RECORD_COUNT
        )
        self.assertTrue(
            all(value.startswith('"WORR_NATIVE_RELIABLE_PRINT_')
                for value in printed)
        )
        control = command[command.index("+stuffall") + 1]
        self.assertNotIn("worr_dm_initial_spectate", control)
        self.assertIn("cl_worr_native_shadow_probe_hold 0", control)
        self.assertIn("cl_worr_native_shadow_probe_hold 1", control)
        self.assertIn("echo client_done", control)
        self.assertLess(
            len(subprocess.list2cmdline(command)),
            SMOKE.WINDOWS_COMMAND_LINE_LIMIT,
        )

    def test_client_stays_held_until_stuffed_relative_control(self) -> None:
        command = SMOKE.client_command(Path("client.exe"), port=28017)
        settings = {
            command[index + 1]: command[index + 2]
            for index, value in enumerate(command[:-2])
            if value == "+set"
        }
        self.assertEqual(settings["win_headless"], "1")
        self.assertEqual(settings["in_enable"], "0")
        self.assertEqual(settings["in_grab"], "0")
        early_hold = [
            index for index, value in enumerate(command[:-2])
            if value == "+set"
            and command[index + 1] == "cl_worr_native_shadow_probe_hold"
        ]
        self.assertEqual(len(early_hold), 1)
        self.assertEqual(command[early_hold[0] + 2], "1")
        self.assertNotIn("+cl_worr_native_shadow_probe_hold", command)
        control = SMOKE.client_control_payload("client_done")
        release = control.index("cl_worr_native_shadow_probe_hold 0")
        rehold = control.index("cl_worr_native_shadow_probe_hold 1")
        first_wait = control.index(
            f"wait {SMOKE.FRAGMENT_CLIENT_RELEASE_WAIT_FRAMES}"
        )
        status = control.index("cl_worr_native_shadow_status")
        self.assertLess(first_wait, release)
        self.assertLess(release, rehold)
        self.assertLess(rehold, status)

    def test_async_trial_uses_proven_post_burst_profile(self) -> None:
        command = SMOKE.server_command(
            Path("server.exe"),
            port=28018,
            client_completion_marker="client_done",
            server_completion_marker="server_done",
            reliable_record_count=SMOKE.ASYNC_RELIABLE_RECORD_COUNT,
            client_release_wait_frames=(
                SMOKE.ASYNC_CLIENT_RELEASE_WAIT_FRAMES
            ),
            rate_bytes_per_second=(
                SMOKE.ASYNC_RATE_BYTES_PER_SECOND
            ),
        )
        settings = {
            command[index + 1]: command[index + 2]
            for index, value in enumerate(command[:-2])
            if value == "+set"
        }
        self.assertEqual(
            settings["sv_min_rate"],
            str(SMOKE.ASYNC_RATE_BYTES_PER_SECOND),
        )
        self.assertEqual(
            settings["sv_max_rate"],
            str(SMOKE.ASYNC_RATE_BYTES_PER_SECOND),
        )
        client = SMOKE.client_command(
            Path("client.exe"),
            port=28018,
            rate_bytes_per_second=SMOKE.ASYNC_RATE_BYTES_PER_SECOND,
        )
        self.assertEqual(
            client[client.index("rate") + 1],
            str(SMOKE.ASYNC_RATE_BYTES_PER_SECOND),
        )
        printed = [
            value for index, value in enumerate(command)
            if index and command[index - 1] == "+printall"
        ]
        self.assertEqual(len(printed), 8)
        control = command[command.index("+stuffall") + 1]
        self.assertIn("wait 186", control)


class NativeShadowReportHashTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.run_id = "unit-report"
        self.profile = valid_profile()
        self.runtime_records = []
        runtime_paths = {}
        for role in SMOKE.RUNTIME_ROLES:
            path = self.root / f"runtime-{role}.bin"
            path.write_bytes(role.encode("ascii"))
            self.runtime_records.append(SMOKE.make_file_record(role, path))
            runtime_paths[role] = path.resolve()
        self.log_records = []
        trials = {}
        for spec in SMOKE.TRIAL_SPECS:
            trial_profile = self.profile["trials"][spec.name]
            port = trial_profile["port"]
            client_marker = (
                f"worr_native_shadow_{spec.name}_client_complete_"
                f"{self.run_id}"
            )
            server_marker = (
                f"worr_native_shadow_{spec.name}_server_complete_"
                f"{self.run_id}"
            )
            client_text, server_text = runtime_texts(
                port=port,
                client_marker=client_marker,
                server_marker=server_marker,
                reliable_record_count=spec.reliable_record_count,
            )
            for endpoint, stream, text in (
                ("client", "stdout", client_text),
                ("client", "stderr", ""),
                ("server", "stdout", server_text),
                ("server", "stderr", ""),
            ):
                role = f"{spec.name}_{endpoint}_{stream}"
                path = self.root / f"{role}.log"
                path.write_text(text, encoding="utf-8")
                self.log_records.append(SMOKE.make_file_record(role, path))
            evidence = SMOKE.validate_runtime_text(
                client_text,
                server_text,
                client_marker=client_marker,
                server_marker=server_marker,
                port=port,
                reliable_record_count=spec.reliable_record_count,
                required_server_evidence=spec.required_server_evidence,
            )
            client_argv = SMOKE.client_command(
                runtime_paths["client_executable"],
                port=port,
                rate_bytes_per_second=spec.rate_bytes_per_second,
            )
            server_argv = SMOKE.server_command(
                runtime_paths["dedicated_executable"],
                port=port,
                client_completion_marker=client_marker,
                server_completion_marker=server_marker,
                reliable_record_count=spec.reliable_record_count,
                client_release_wait_frames=spec.client_release_wait_frames,
                rate_bytes_per_second=spec.rate_bytes_per_second,
            )
            trials[spec.name] = {
                "statuses": {
                    "client": evidence["client_status"],
                    "server": evidence["server_status"],
                },
                "reliable_delivery": evidence["reliable_delivery"],
                "impairment": {
                    "client": evidence["client_impairment"],
                    "server": evidence["server_impairment"],
                },
                "commands": {
                    "client_argc": len(client_argv),
                    "client_argv_sha256": SMOKE.argv_sha256(client_argv),
                    "server_argc": len(server_argv),
                    "server_argv_sha256": SMOKE.argv_sha256(server_argv),
                },
                "processes": {
                    "client_terminated_by_harness": True,
                    "server_terminated_by_harness": True,
                    "elapsed_seconds": 32.0,
                },
            }
        self.report = {
            "schema": SMOKE.SCHEMA,
            "passed": True,
            "run_id": self.run_id,
            "profile": self.profile,
            "status_contract": {
                "client_prefix": SMOKE.CLIENT_STATUS_PREFIX,
                "client_required_fields": list(SMOKE.CLIENT_STATUS_FIELDS),
                "server_prefix": SMOKE.SERVER_STATUS_PREFIX,
                "server_required_fields": list(SMOKE.SERVER_STATUS_FIELDS),
            },
            "trials": trials,
            "overall_elapsed_seconds": 64.0,
            "runtime_components": self.runtime_records,
            "logs": self.log_records,
            "limitations": ["one", "two", "three", "four"],
        }

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_complete_report_and_hashes_pass(self) -> None:
        SMOKE.validate_report(self.report)

    def test_report_enforces_each_trial_scheduler_contract(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["trials"][SMOKE.FRAGMENT_TRIAL]["statuses"]["server"][
            "async_fragment_deferrals"
        ] = 0
        with self.assertRaisesRegex(RuntimeError, "async_fragment_deferrals"):
            SMOKE.validate_report(changed)
        changed = copy.deepcopy(self.report)
        async_server = changed["trials"][SMOKE.ASYNC_TRIAL]["statuses"][
            "server"
        ]
        async_server["async_wake_attempts"] = 0
        async_server["async_ack_handoffs"] = 0
        with self.assertRaisesRegex(RuntimeError, "async_wake_attempts"):
            SMOKE.validate_report(changed)

    def test_profile_requires_distinct_trial_ports(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["profile"]["trials"][SMOKE.ASYNC_TRIAL]["port"] = 28017
        with self.assertRaisesRegex(RuntimeError, "distinct ports"):
            SMOKE.validate_report(changed)

    def test_wrong_schema_port_and_profile_are_rejected(self) -> None:
        for path, value, pattern in (
            (("schema",), "v0", "schema"),
            (("profile", "trials", SMOKE.FRAGMENT_TRIAL, "port"), 0, "port"),
            (("profile", "protocol"), 36, "protocol"),
            (("profile", "rate_bytes_per_second"), 15000, "rate"),
            (("profile", "trials", SMOKE.ASYNC_TRIAL,
              "rate_bytes_per_second"), 1500, "rate"),
            (("profile", "trials", SMOKE.FRAGMENT_TRIAL,
              "client_rehold_wait_frames"), 3, "rehold"),
            (("profile", "net_maxmsglen"), 1390, "net_maxmsglen"),
        ):
            with self.subTest(path=path):
                changed = copy.deepcopy(self.report)
                target = changed
                for name in path[:-1]:
                    target = target[name]
                target[path[-1]] = value
                with self.assertRaisesRegex(RuntimeError, pattern):
                    SMOKE.validate_report(changed)

    def test_missing_and_duplicate_artifact_roles_are_rejected(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["logs"] = changed["logs"][:-1]
        with self.assertRaisesRegex(RuntimeError, "missing roles"):
            SMOKE.validate_report(changed)
        changed = copy.deepcopy(self.report)
        changed["runtime_components"].append(
            copy.deepcopy(changed["runtime_components"][0])
        )
        with self.assertRaisesRegex(RuntimeError, "duplicates role"):
            SMOKE.validate_report(changed)

    def test_wrong_executable_and_log_hashes_are_rejected(self) -> None:
        for manifest_name in ("runtime_components", "logs"):
            with self.subTest(manifest=manifest_name):
                changed = copy.deepcopy(self.report)
                changed[manifest_name][0]["sha256"] = "0" * 64
                with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                    SMOKE.validate_report(changed)
                changed = copy.deepcopy(self.report)
                changed[manifest_name][0]["sha256"] = "not-a-hash"
                with self.assertRaisesRegex(RuntimeError, "invalid SHA-256"):
                    SMOKE.validate_report(changed)

    def test_changed_hashed_file_is_rejected(self) -> None:
        path = Path(self.report["logs"][0]["path"])
        path.write_text("changed\n", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "(byte count|mtime|SHA-256)"):
            SMOKE.validate_report(self.report)

    def test_command_hash_and_harness_lifetime_are_required(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["trials"][SMOKE.FRAGMENT_TRIAL]["commands"][
            "client_argv_sha256"
        ] = "bad"
        with self.assertRaisesRegex(RuntimeError, "command SHA-256"):
            SMOKE.validate_report(changed)
        changed = copy.deepcopy(self.report)
        changed["trials"][SMOKE.FRAGMENT_TRIAL]["commands"][
            "client_argv_sha256"
        ] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "argv binding"):
            SMOKE.validate_report(changed)
        changed = copy.deepcopy(self.report)
        changed["trials"][SMOKE.ASYNC_TRIAL]["processes"][
            "server_terminated_by_harness"
        ] = False
        with self.assertRaisesRegex(RuntimeError, "harness-terminated"):
            SMOKE.validate_report(changed)

    def test_hashed_logs_are_reparsed_and_bound_to_report_evidence(self) -> None:
        changed = copy.deepcopy(self.report)
        role = f"{SMOKE.FRAGMENT_TRIAL}_client_stdout"
        index = next(
            index for index, record in enumerate(changed["logs"])
            if record["role"] == role
        )
        path = Path(changed["logs"][index]["path"])
        text = path.read_text(encoding="utf-8")
        path.write_text(
            text.replace("seen=200", "seen=201"), encoding="utf-8"
        )
        changed["logs"][index] = SMOKE.make_file_record(role, path)
        with self.assertRaisesRegex(RuntimeError, "hashed logs"):
            SMOKE.validate_report(changed)

    def test_run_id_must_be_a_safe_marker_token(self) -> None:
        changed = copy.deepcopy(self.report)
        changed["run_id"] = "unsafe; quit"
        with self.assertRaisesRegex(RuntimeError, "run ID"):
            SMOKE.validate_report(changed)

    def test_previous_pass_and_failure_are_invalidated(self) -> None:
        output = self.root / "native-shadow-runtime.json"
        failure = output.with_suffix(".failure.json")
        output.write_text("stale pass", encoding="utf-8")
        failure.write_text("stale failure", encoding="utf-8")
        self.assertEqual(SMOKE.invalidate_previous_outputs(output), failure)
        self.assertFalse(output.exists())
        self.assertFalse(failure.exists())


if __name__ == "__main__":
    unittest.main()
