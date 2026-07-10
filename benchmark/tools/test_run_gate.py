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
                owner_approved_generated_calibration=False,
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
            gate.finalize("PASS", 0)
            manifest = json.loads(
                (attempt / "gate_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["status"], "PASS")
            self.assertIn("evidence.txt", manifest["artifact_hashes"])
            self.assertNotIn("gate_manifest.json", manifest["artifact_hashes"])


if __name__ == "__main__":
    unittest.main()
