#!/usr/bin/env python3
"""Record explicit owner approval for one retained calibration proposal hash."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from run_gate import sha256_file, utc_now, write_json


REQUIRED_PASS_PHASES = {
    "style",
    "actionlint",
    "configure",
    "build",
    "ctest",
    "retain-smoke-evidence",
    "calibration",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt-dir", type=Path, required=True)
    parser.add_argument("--proposal-sha256", required=True)
    parser.add_argument("--approved-by", required=True)
    return parser.parse_args()


def retained_hashes(attempt_dir: Path) -> dict[str, str]:
    return {
        path.relative_to(attempt_dir).as_posix(): sha256_file(path)
        for path in sorted(attempt_dir.rglob("*"))
        if path.is_file() and path.name != "gate_manifest.json"
    }


def approve(
    attempt_dir: Path,
    proposal_sha256: str,
    approved_by: str,
) -> dict[str, Any]:
    attempt_dir = attempt_dir.resolve()
    manifest_path = attempt_dir / "gate_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("status") != "CALIBRATION_REVIEW_REQUIRED":
        raise RuntimeError("gate is not awaiting calibration approval")
    if manifest.get("inputs", {}).get("source", {}).get("dirty"):
        raise RuntimeError("dirty source evidence cannot be approved")
    dependency = manifest.get("inputs", {}).get("preflight", {}).get("dependency", {})
    if dependency.get("dirty", True):
        raise RuntimeError("dirty dependency evidence cannot be approved")

    phases = {phase.get("name"): phase.get("status") for phase in manifest.get("phases", [])}
    missing_or_failed = {
        phase: phases.get(phase)
        for phase in REQUIRED_PASS_PHASES
        if phases.get(phase) != "PASS"
    }
    if missing_or_failed:
        raise RuntimeError(f"gate phases are incomplete: {missing_or_failed}")

    proposal_entry = manifest.get("inputs", {}).get("calibration_proposal", {})
    relative_proposal = Path(proposal_entry.get("path", ""))
    proposal_path = (attempt_dir / relative_proposal).resolve()
    if attempt_dir not in proposal_path.parents:
        raise RuntimeError("calibration proposal escapes the retained gate attempt")
    actual_sha256 = sha256_file(proposal_path)
    expected_sha256 = proposal_entry.get("sha256")
    if not proposal_sha256 or proposal_sha256 != actual_sha256:
        raise RuntimeError("supplied proposal SHA256 does not match retained evidence")
    if expected_sha256 != actual_sha256:
        raise RuntimeError("gate manifest proposal SHA256 does not match retained evidence")

    approval_path = attempt_dir / "calibration" / "owner_approval.json"
    if approval_path.exists():
        raise RuntimeError("owner approval is already recorded")
    recorded_at = utc_now()
    approval = {
        "schema_version": 1,
        "status": "OWNER_APPROVED",
        "approved_by": approved_by,
        "recorded_at_utc": recorded_at,
        "proposal": relative_proposal.as_posix(),
        "proposal_sha256": actual_sha256,
        "source_commit": manifest["inputs"]["source"]["commit"],
    }
    approval_path.parent.mkdir(parents=True, exist_ok=True)
    with approval_path.open("x", encoding="utf-8") as output:
        output.write(json.dumps(approval, indent=2, sort_keys=True) + "\n")

    manifest["status"] = "PASS"
    manifest["exit_status"] = 0
    manifest["ended_at_utc"] = recorded_at
    manifest["current_phase"] = "owner-approval"
    manifest.pop("reason", None)
    manifest.setdefault("phases", []).append(
        {
            "name": "owner-approval",
            "status": "PASS",
            "started_at_utc": recorded_at,
            "ended_at_utc": recorded_at,
            "command": [],
            "returncode": 0,
            "proposal_sha256": actual_sha256,
            "approved_by": approved_by,
        }
    )
    manifest.setdefault("status_history", []).append(
        {"status": "PASS", "recorded_at_utc": recorded_at}
    )
    manifest["inputs"]["owner_approval"] = {
        "path": approval_path.relative_to(attempt_dir).as_posix(),
        "sha256": sha256_file(approval_path),
    }
    manifest["artifact_hashes"] = retained_hashes(attempt_dir)
    write_json(manifest_path, manifest)
    return manifest


def main() -> int:
    args = parse_args()
    manifest = approve(
        args.attempt_dir,
        args.proposal_sha256.lower(),
        args.approved_by,
    )
    print(Path(args.attempt_dir).resolve())
    print(f"status={manifest['status']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
