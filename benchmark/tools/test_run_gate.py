#!/usr/bin/env python3
"""Regression tests for append-only checkpoint gate evidence."""

from __future__ import annotations

import argparse
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("run_gate.py")
SPEC = importlib.util.spec_from_file_location("vnm_plot_run_gate", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {MODULE_PATH}")
run_gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(run_gate)


class GateEvidenceTests(unittest.TestCase):
    def test_attempt_identity_is_unique(self) -> None:
        first = run_gate.attempt_identity()
        second = run_gate.attempt_identity()
        self.assertNotEqual(first, second)
        self.assertRegex(first, r"^\d{8}T\d{6}\.\d{6}Z-[0-9a-f]{8}$")

    def test_finalize_hashes_artifacts_without_self_reference(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            attempt = root / "attempt"
            attempt.mkdir()
            args = argparse.Namespace(
                batch="batch-2",
                checkpoint="2.1",
                mode="ci-retain",
                recovery_of=None,
                build_dir=root / "build",
                config="Release",
                source_root=root,
            )
            source = {
                "commit": "a" * 40,
                "slug": "source",
                "dirty": False,
                "diff": "",
            }
            gate = run_gate.Gate(args, attempt, source, {"status": "test"})
            evidence = attempt / "evidence.txt"
            evidence.write_text("retained\n", encoding="utf-8")
            gate.finalize("DIAGNOSTIC_PASS", 0)
            manifest = json.loads(
                (attempt / "gate_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["status"], "DIAGNOSTIC_PASS")
            self.assertIn("evidence.txt", manifest["artifact_hashes"])
            self.assertNotIn("gate_manifest.json", manifest["artifact_hashes"])

    def test_finalize_rejects_unapproved_checkpoint_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            attempt = root / "attempt"
            attempt.mkdir()
            args = argparse.Namespace(
                batch="batch-2",
                checkpoint="2.1",
                mode="full",
                recovery_of=None,
                build_dir=root / "build",
                config="Release",
                source_root=root,
            )
            source = {
                "commit": "a" * 40,
                "slug": "source",
                "dirty": False,
                "diff": "",
            }
            gate = run_gate.Gate(args, attempt, source, {"status": "test"})
            with self.assertRaisesRegex(RuntimeError, "approve_gate.py"):
                gate.finalize("PASS", 0)

    def test_multi_config_executable_is_resolved(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            executable_name = (
                "vnm_plot_benchmark.exe"
                if run_gate.os.name == "nt"
                else "vnm_plot_benchmark"
            )
            executable = build / "benchmark" / "Release" / executable_name
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"benchmark")
            self.assertEqual(
                run_gate.resolve_benchmark_executable(build, "Release"),
                executable.resolve(),
            )

    def test_ambiguous_executable_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            executable_name = (
                "vnm_plot_benchmark.exe"
                if run_gate.os.name == "nt"
                else "vnm_plot_benchmark"
            )
            for executable in (
                build / "benchmark" / executable_name,
                build / "benchmark" / "Release" / executable_name,
            ):
                executable.parent.mkdir(parents=True, exist_ok=True)
                executable.write_bytes(b"benchmark")
            with self.assertRaises(RuntimeError):
                run_gate.resolve_benchmark_executable(build, "Release")


if __name__ == "__main__":
    unittest.main()
