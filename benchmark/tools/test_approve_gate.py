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
            "artifact_hashes": approve_gate.retained_hashes(attempt),
        }
        approve_gate.write_json(attempt / "gate_manifest.json", manifest)
        return attempt, proposal_sha256

    def test_matching_hash_records_owner_approval_and_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            attempt, proposal_sha256 = self.make_attempt(Path(temporary))
            manifest = approve_gate.approve(attempt, proposal_sha256, "repository-owner")
            self.assertEqual(manifest["status"], "PASS")
            self.assertTrue((attempt / "calibration" / "owner_approval.json").is_file())
            repeated = approve_gate.approve(
                attempt,
                proposal_sha256,
                "repository-owner",
            )
            self.assertEqual(repeated["status"], "PASS")

    def test_wrong_hash_is_rejected_without_approval(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            attempt, _ = self.make_attempt(Path(temporary))
            with self.assertRaises(RuntimeError):
                approve_gate.approve(attempt, "0" * 64, "repository-owner")
            self.assertFalse((attempt / "calibration" / "owner_approval.json").exists())

    def test_changed_nonproposal_artifact_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            attempt, proposal_sha256 = self.make_attempt(Path(temporary))
            evidence = attempt / "smoke" / "validation.json"
            evidence.parent.mkdir()
            evidence.write_text('{"status":"PASS"}\n', encoding="utf-8")
            manifest_path = attempt / "gate_manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["artifact_hashes"] = approve_gate.retained_hashes(attempt)
            approve_gate.write_json(manifest_path, manifest)
            evidence.write_text('{"status":"FAIL"}\n', encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "artifacts changed"):
                approve_gate.approve(attempt, proposal_sha256, "repository-owner")

    def test_interrupted_owner_record_can_complete_same_transition(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            attempt, proposal_sha256 = self.make_attempt(Path(temporary))
            approval_path = attempt / "calibration" / "owner_approval.json"
            approval = {
                "schema_version": 1,
                "status": "OWNER_APPROVED",
                "approved_by": "repository-owner",
                "recorded_at_utc": "2026-07-10T00:00:00+00:00",
                "proposal": "calibration/proposed_noise_margins.json",
                "proposal_sha256": proposal_sha256,
                "source_commit": "a" * 40,
            }
            approval_path.write_text(
                json.dumps(approval, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            manifest = approve_gate.approve(
                attempt,
                proposal_sha256,
                "repository-owner",
            )
            self.assertEqual(manifest["status"], "PASS")
            self.assertEqual(
                manifest["phases"][-1]["started_at_utc"],
                "2026-07-10T00:00:00+00:00",
            )


if __name__ == "__main__":
    unittest.main()
