#!/usr/bin/env python3
"""Regression tests for explicit retained calibration approval."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))
MODULE_PATH = TOOLS_DIR / "approve_gate.py"
SPEC = importlib.util.spec_from_file_location("vnm_plot_approve_gate", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {MODULE_PATH}")
approve_gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(approve_gate)


class GateApprovalTests(unittest.TestCase):
    def make_attempt(self, root: Path) -> tuple[Path, str]:
        attempt = root / "attempt"
        proposal = attempt / "calibration" / "proposed_noise_margins.json"
        proposal.parent.mkdir(parents=True)
        proposal.write_text('{"status":"CALIBRATION_REVIEW_REQUIRED"}\n', encoding="utf-8")
        proposal_sha256 = approve_gate.sha256_file(proposal)
        phases = [
            {"name": name, "status": "PASS"}
            for name in sorted(approve_gate.REQUIRED_PASS_PHASES)
        ]
        manifest = {
            "status": "CALIBRATION_REVIEW_REQUIRED",
            "exit_status": 2,
            "inputs": {
                "source": {"commit": "a" * 40, "dirty": False},
                "preflight": {"dependency": {"dirty": False}},
                "calibration_proposal": {
                    "path": "calibration/proposed_noise_margins.json",
                    "sha256": proposal_sha256,
                },
            },
            "phases": phases,
            "status_history": [],
        }
        approve_gate.write_json(attempt / "gate_manifest.json", manifest)
        return attempt, proposal_sha256

    def test_matching_hash_records_owner_approval_and_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            attempt, proposal_sha256 = self.make_attempt(Path(temporary))
            manifest = approve_gate.approve(attempt, proposal_sha256, "repository-owner")
            self.assertEqual(manifest["status"], "PASS")
            self.assertTrue((attempt / "calibration" / "owner_approval.json").is_file())

    def test_wrong_hash_is_rejected_without_approval(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            attempt, _ = self.make_attempt(Path(temporary))
            with self.assertRaises(RuntimeError):
                approve_gate.approve(attempt, "0" * 64, "repository-owner")
            self.assertFalse((attempt / "calibration" / "owner_approval.json").exists())


if __name__ == "__main__":
    unittest.main()
