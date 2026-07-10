#!/usr/bin/env python3
"""Regression tests for retained calibration protocol and arithmetic."""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("calibrate.py")
SPEC = importlib.util.spec_from_file_location("vnm_plot_calibrate", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {MODULE_PATH}")
calibrate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(calibrate)


class CalibrationArithmeticTests(unittest.TestCase):
    def test_relative_margin_is_exact_maximum_set_median_drift(self) -> None:
        rule = calibrate.propose_rule(
            {"unit": "ms"},
            [[100.0] * 7, [110.0] * 7],
            resolution=0.001,
            unstable_threshold=0.25,
        )
        self.assertEqual(rule["rule"], "relative")
        self.assertTrue(math.isclose(rule["relative_margin"], 0.1))
        self.assertTrue(math.isclose(rule["absolute_margin"], 10.0))

    def test_relative_margin_is_not_capped_and_marks_instability(self) -> None:
        rule = calibrate.propose_rule(
            {"unit": "ms"},
            [[1.0] * 7, [3.0] * 7],
            resolution=0.001,
            unstable_threshold=0.25,
        )
        self.assertTrue(math.isclose(rule["relative_margin"], 2.0))
        self.assertEqual(rule["status"], "CALIBRATION_REVIEW_REQUIRED")

    def test_designated_deterministic_zero_is_exact(self) -> None:
        rule = calibrate.propose_rule(
            {"unit": "count", "deterministic_zero": True},
            [[0.0] * 7, [0.0] * 7],
            resolution=1.0,
            unstable_threshold=0.25,
        )
        self.assertEqual(rule["rule"], "exact_zero")
        self.assertEqual(rule["absolute_margin"], 0.0)
        self.assertIsNone(rule["relative_margin"])

    def test_sub_resolution_value_has_only_absolute_rule(self) -> None:
        rule = calibrate.propose_rule(
            {"unit": "ns"},
            [[2.0] * 7, [3.0] * 7],
            resolution=10.0,
            unstable_threshold=0.25,
        )
        self.assertEqual(rule["rule"], "absolute")
        self.assertEqual(rule["absolute_margin"], 10.0)
        self.assertIsNone(rule["relative_margin"])
        self.assertEqual(rule["status"], "CALIBRATION_REVIEW_REQUIRED")


class CalibrationProtocolTests(unittest.TestCase):
    def make_args(self, output_dir: Path) -> argparse.Namespace:
        return argparse.Namespace(
            executable=output_dir / "benchmark",
            output_dir=output_dir,
            graphics_backend="native",
            width=1200,
            height=720,
            warmup_runs=2,
            warmup_frames=2,
            frames=120,
            sets=2,
            runs_per_set=7,
            timeout_seconds=120.0,
            unstable_drift_threshold=0.25,
            manifest_only=True,
            allow_dirty_source=False,
        )

    def test_manifest_records_fixed_protocol_and_scenarios(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            manifest = calibrate.scenario_manifest(self.make_args(Path(temporary)))
        protocol = manifest["protocol"]
        self.assertEqual(protocol["calibration_sets"], 2)
        self.assertEqual(protocol["warmup_runs_per_set"], 2)
        self.assertEqual(protocol["measured_runs_per_set"], 7)
        self.assertEqual(len(manifest["scenarios"]), 6)
        self.assertEqual(manifest["status"], "CALIBRATION_REVIEW_REQUIRED")

    def test_attempt_identities_are_unique_and_append_only_shaped(self) -> None:
        first = calibrate.attempt_identity()
        second = calibrate.attempt_identity()
        self.assertNotEqual(first, second)
        self.assertTrue(first.startswith("attempt-"))
        self.assertTrue(second.startswith("attempt-"))

    def test_resume_rejects_a_mismatched_runner_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output_dir = Path(temporary)
            args = self.make_args(output_dir)
            args.executable.write_bytes(b"benchmark")
            scenario = calibrate.make_scenarios()[0]
            run_id = "set-1-measured-1"
            run_root = output_dir / "calibration-runs" / scenario["id"] / run_id
            attempt = run_root / "attempt-old"
            attempt.mkdir(parents=True)
            (attempt / "inspector_benchmark_old.json").write_text(
                "{}\n",
                encoding="utf-8",
            )
            calibrate.write_json(
                attempt / "invocation.json",
                {
                    "command": calibrate.make_command(
                        args,
                        scenario,
                        run_id,
                        attempt,
                    ),
                    "fingerprint": {"runner_sha256": "stale"},
                },
            )
            retained = calibrate.retained_success(
                args,
                scenario,
                run_root,
                run_id,
                calibrate.run_fingerprint(args, scenario, run_id),
            )
            self.assertIsNone(retained)

    def test_manifest_only_writes_parseable_json(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output_dir = Path(temporary)
            args = self.make_args(output_dir)
            manifest = calibrate.scenario_manifest(args)
            path = output_dir / "scenario_manifest.json"
            calibrate.write_json(path, manifest)
            self.assertEqual(path.read_text(encoding="utf-8")[-1], "\n")

    def test_retained_manifest_is_not_overwritten(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "manifest.json"
            first = {"generated_at_utc": "first", "protocol": {"sets": 2}}
            second = {"generated_at_utc": "second", "protocol": {"sets": 2}}
            retained = calibrate.write_immutable_json(path, first)
            reused = calibrate.write_immutable_json(path, second)
            self.assertEqual(retained, first)
            self.assertEqual(reused, first)
            self.assertEqual(
                json.loads(path.read_text(encoding="utf-8")),
                first,
            )

    def test_incompatible_retained_manifest_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "manifest.json"
            calibrate.write_immutable_json(path, {"protocol": {"sets": 2}})
            with self.assertRaises(RuntimeError):
                calibrate.write_immutable_json(path, {"protocol": {"sets": 3}})


if __name__ == "__main__":
    unittest.main()
