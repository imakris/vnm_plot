#!/usr/bin/env python3
"""Run and validate a deterministic native/backend QRhi smoke artifact."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graphics-backend", default="native")
    parser.add_argument("--frames", type=int, default=3)
    parser.add_argument("--allow-unavailable", action="store_true")
    return parser.parse_args()


def normalized_backend(name: str) -> str:
    normalized = "".join(character.lower() for character in name if character.isalnum())
    if normalized in {"opengles2", "gles", "gles2"}:
        return "opengl"
    if normalized == "direct3d11":
        return "d3d11"
    return normalized


def expected_native_backend() -> str:
    if sys.platform == "win32":
        return "d3d11"
    if sys.platform == "darwin":
        return "metal"
    return "opengl"


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def observation_total(payload: dict, name: str) -> float:
    return float(payload["observations"][name]["total"])


def validate(payload: dict, args: argparse.Namespace, attempt_dir: Path) -> None:
    metadata = payload["metadata"]
    actual = normalized_backend(metadata["actual_graphics_backend"])
    expected = expected_native_backend() if args.graphics_backend == "native" else args.graphics_backend
    if actual != normalized_backend(expected):
        raise RuntimeError(f"expected backend {expected}, got {metadata['actual_graphics_backend']}")
    if int(metadata["measured_frames"]) != args.frames:
        raise RuntimeError("metadata measured-frame count mismatch")
    if int(metadata["pixel_checksum"]) == 0:
        raise RuntimeError("pixel checksum is zero")
    if int(metadata["pixel_nonuniform_count"]) == 0:
        raise RuntimeError("readback is indistinguishable from a clear-only frame")
    if observation_total(payload, "benchmark.frame.output_count") != args.frames:
        raise RuntimeError("output counter mismatch")
    if observation_total(payload, "benchmark.ring.published_samples") != 0:
        raise RuntimeError("static smoke includes pre-measurement publications")
    if observation_total(payload, "renderer.frame.upload.primary_count") != 0:
        raise RuntimeError("static unchanged primary uploads are not zero")
    if observation_total(payload, "renderer.frame.upload.line_window_count") <= 0:
        raise RuntimeError("LINE-window upload counter is missing")
    if observation_total(payload, "renderer.frame.upload.total_bytes") <= 0:
        raise RuntimeError("total upload bytes are missing")
    if observation_total(payload, "benchmark.snapshot.count") <= 0:
        raise RuntimeError("snapshot counter is missing")
    deterministic_zeros = (
        "benchmark.ring.overwritten_samples",
        "renderer.frame.buffer_allocation_count",
        "renderer.series_window.monotonicity_scan_count",
        "renderer.series_window.monotonicity_scan_samples",
        "renderer.frame.upload.known_custom_count",
    )
    for observation in deterministic_zeros:
        if observation_total(payload, observation) != 0:
            raise RuntimeError(f"deterministic counter {observation} is not zero")
    trace_path = Path(metadata["phase_trace_path"])
    if not trace_path.is_absolute():
        trace_path = attempt_dir / trace_path.name
    if not trace_path.is_file():
        raise RuntimeError("phase trace is missing")


def main() -> int:
    args = parse_args()
    attempt = args.output_dir / (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ-") + uuid.uuid4().hex[:8]
    )
    attempt.mkdir(parents=True, exist_ok=False)
    command = [
        str(args.executable.resolve()),
        "--backend", "qrhi-offscreen",
        "--graphics-backend", args.graphics_backend,
        "--static",
        "--data-type", "bars",
        "--render-style", "line",
        "--seed", "12345",
        "--warmup-frames", "2",
        "--frames", str(args.frames),
        "--finish",
        "--pixel-checksum",
        "--quiet",
        "--output-dir", str(attempt.resolve()),
        "--scenario", f"ci-{args.graphics_backend}-smoke",
    ]
    completed = subprocess.run(command, check=False, capture_output=True, text=True, timeout=60)
    write_json(
        attempt / "smoke_invocation.json",
        {
            "command": command,
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        },
    )
    if completed.returncode != 0:
        if args.allow_unavailable and "backend_create" in completed.stderr:
            write_json(
                attempt / "smoke_validation.json",
                {"status": "UNAVAILABLE", "reason": completed.stderr.strip()},
            )
            return 0
        raise RuntimeError(completed.stderr.strip() or "benchmark smoke failed")
    artifacts = list(attempt.glob("inspector_benchmark_*.json"))
    if len(artifacts) != 1:
        raise RuntimeError(f"expected one raw artifact, found {len(artifacts)}")
    payload = json.loads(artifacts[0].read_text(encoding="utf-8"))
    validate(payload, args, attempt)
    write_json(
        attempt / "smoke_validation.json",
        {
            "status": "PASS",
            "artifact": artifacts[0].name,
            "backend": payload["metadata"]["actual_graphics_backend"],
            "pixel_checksum": payload["metadata"]["pixel_checksum"],
            "pixel_nonuniform_count": payload["metadata"]["pixel_nonuniform_count"],
        },
    )
    print(attempt.resolve())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI reports validation failure.
        print(f"smoke validation failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
