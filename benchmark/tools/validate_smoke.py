#!/usr/bin/env python3
"""Run and validate a deterministic native/backend QRhi smoke artifact."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path

from phase_trace import parse_and_validate_phase_trace


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graphics-backend", default="native")
    parser.add_argument("--frames", type=int, default=3)
    parser.add_argument("--stacked", action="store_true")
    return parser.parse_args()


def normalized_backend(name: str) -> str:
    normalized = "".join(character.lower() for character in name if character.isalnum())
    if normalized in {"opengles2", "gles", "gles2"}:
        return "opengl"
    if normalized == "direct3d11":
        return "d3d11"
    return normalized


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def captured_text(value: object) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value or "")


def observation_total(payload: dict, name: str) -> float:
    return float(payload["observations"][name]["total"])


def validate(
    payload: dict,
    args: argparse.Namespace,
    attempt_dir: Path,
    expected_command: list[str],
) -> Path:
    metadata = payload["metadata"]
    if payload.get("invocation_args") != expected_command:
        raise RuntimeError("structured smoke invocation mismatch")
    expected_metadata = {
        "context_profile_request": "core",
        "context_sample_count": "1",
        "context_version_request": "3.3",
        "data_type": "Bars",
        "finish_state": "enabled",
        "framebuffer": "1200x720",
        "rate": "1000.000000",
        "render_style": "Area" if args.stacked else "Line",
        "requested_graphics_backend": args.graphics_backend,
        "sample_count": "4",
        "scenario": f"ci-{args.graphics_backend}-stacked-area-smoke"
        if args.stacked else f"ci-{args.graphics_backend}-smoke",
        "seed": "12345",
        "series_count": "2" if args.stacked else "1",
        "show_text": "true",
        "static_data": "true",
        "static_sample_count": "10000",
        "warmup_frames": "2",
    }
    mismatches = {
        field: {"expected": expected, "actual": metadata.get(field)}
        for field, expected in expected_metadata.items()
        if metadata.get(field) != expected
    }
    if mismatches:
        raise RuntimeError(f"smoke workload metadata mismatch: {mismatches}")
    actual = normalized_backend(metadata["actual_graphics_backend"])
    if args.graphics_backend == "native":
        if actual in {"", "null", "uninitialized"}:
            raise RuntimeError(f"native QRhi selected {metadata['actual_graphics_backend']}")
    elif actual != normalized_backend(args.graphics_backend):
        raise RuntimeError(
            f"expected backend {args.graphics_backend}, "
            f"got {metadata['actual_graphics_backend']}"
        )
    if actual == "opengl":
        if metadata.get("fallback_surface_requested_format") != "3.3|core|1":
            raise RuntimeError("OpenGL fallback-surface request changed")
        if metadata.get("fallback_surface_resolved_format") in (None, "not-applicable"):
            raise RuntimeError("OpenGL fallback-surface resolution is missing")
    if int(metadata["measured_frames"]) != args.frames:
        raise RuntimeError("metadata measured-frame count mismatch")
    if metadata.get("static_sample_count") != "10000":
        raise RuntimeError("static smoke sample-count mismatch")
    if int(metadata["pixel_checksum"]) == 0:
        raise RuntimeError("pixel checksum is zero")
    if int(metadata["pixel_nonuniform_count"]) == 0:
        raise RuntimeError("readback is indistinguishable from a clear-only frame")
    if observation_total(payload, "benchmark.frame.output_count") != args.frames:
        raise RuntimeError("output counter mismatch")
    if observation_total(payload, "benchmark.ring.published_samples") != 0:
        raise RuntimeError("static smoke includes pre-measurement publications")
    if args.stacked:
        if observation_total(payload, "renderer.stacking.output_sample_count") <= 0:
            raise RuntimeError("stacked AREA composition was not exercised")
        if int(metadata["stack_sum_pixel_count"]) <= 0:
            raise RuntimeError("stack sum overlay color #E6DFCC is absent from readback")
    else:
        if observation_total(payload, "renderer.frame.upload.primary_count") != 0:
            raise RuntimeError("static unchanged primary uploads are not zero")
        if observation_total(payload, "renderer.frame.upload.line_window_count") <= 0:
            raise RuntimeError("LINE-window upload counter is missing")
    if observation_total(payload, "renderer.frame.upload.total_bytes") <= 0:
        raise RuntimeError("total upload bytes are missing")
    if observation_total(payload, "benchmark.snapshot.count") <= 0:
        raise RuntimeError("snapshot counter is missing")
    if observation_total(payload, "benchmark.snapshot.view_bytes") < 10_000:
        raise RuntimeError("static smoke did not expose the representative data window")
    deterministic_zeros = (
        "benchmark.ring.overwritten_samples",
        "benchmark.snapshot.copied_bytes",
        "renderer.frame.gpu_buffer_allocation_count",
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
    trace_path = trace_path.resolve()
    if trace_path.parent != attempt_dir.resolve():
        raise RuntimeError("phase trace escapes its smoke attempt")
    if not trace_path.is_file():
        raise RuntimeError("phase trace is missing")
    parse_and_validate_phase_trace(
        trace_path.read_text(encoding="utf-8"),
        int(metadata["warmup_frames"]),
        args.frames,
    )
    return trace_path


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
        "--render-style", "area" if args.stacked else "line",
        "--seed", "12345",
        "--static-samples", "10000",
        "--warmup-frames", "2",
        "--frames", str(args.frames),
        "--finish",
        "--pixel-checksum",
        "--quiet",
        "--output-dir", str(attempt.resolve()),
        "--scenario", f"ci-{args.graphics_backend}-stacked-area-smoke"
        if args.stacked else f"ci-{args.graphics_backend}-smoke",
    ]
    if args.stacked:
        command += ["--series-count", "2", "--stack-series"]
    renderer_environment = {
        name: os.environ.get(name, "")
        for name in ("GALLIUM_DRIVER", "LP_NUM_THREADS")
    }
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=60,
        )
        write_json(
            attempt / "smoke_invocation.json",
            {
                "command": command,
                "renderer_environment": renderer_environment,
                "returncode": completed.returncode,
                "stdout": completed.stdout,
                "stderr": completed.stderr,
            },
        )
        if completed.returncode != 0:
            raise RuntimeError(completed.stderr.strip() or "benchmark smoke failed")
        artifacts = list(attempt.glob("inspector_benchmark_*.json"))
        if len(artifacts) != 1:
            raise RuntimeError(f"expected one raw artifact, found {len(artifacts)}")
        payload = json.loads(artifacts[0].read_text(encoding="utf-8"))
        trace_path = validate(payload, args, attempt, command)
        write_json(
            attempt / "smoke_validation.json",
            {
                "status": "PASS",
                "artifact": artifacts[0].name,
                "phase_trace": trace_path.name,
                "backend": payload["metadata"]["actual_graphics_backend"],
                "pixel_checksum": payload["metadata"]["pixel_checksum"],
                "pixel_nonuniform_count": payload["metadata"]["pixel_nonuniform_count"],
            },
        )
    except Exception as exc:
        if not (attempt / "smoke_invocation.json").exists():
            timeout_payload = {
                "command": command,
                "renderer_environment": renderer_environment,
                "returncode": None,
                "timeout_seconds": 60,
                "stdout": captured_text(getattr(exc, "stdout", "")),
                "stderr": captured_text(getattr(exc, "stderr", "")),
            }
            write_json(attempt / "smoke_invocation.json", timeout_payload)
        write_json(
            attempt / "smoke_validation.json",
            {"status": "FAIL", "reason": str(exc)},
        )
        raise
    print(attempt.resolve())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI reports validation failure.
        print(f"smoke validation failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
