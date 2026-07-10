#!/usr/bin/env python3
"""Run the retained vnm_plot calibration protocol and propose noise margins."""

from __future__ import annotations

import argparse
import json
import math
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_METRICS = (
    "benchmark.frame.total_ms",
    "benchmark.frame.submission_ms",
    "benchmark.frame.gpu_finish_ms",
    "benchmark.planning.time_ms",
    "benchmark.snapshot.time_ns",
    "benchmark.snapshot.count",
    "benchmark.snapshot.bytes",
    "benchmark.producer.lock_wait_ns",
    "benchmark.ring.published_samples_per_second",
    "benchmark.ring.overwritten_samples",
    "benchmark.memory.process_high_water_bytes",
    "renderer.series_window.monotonicity_scan_samples",
    "renderer.frame.buffer_allocation_bytes",
    "renderer.frame.upload.primary_bytes",
    "renderer.frame.upload.line_window_bytes",
    "renderer.frame.upload.uniform_bytes",
    "renderer.frame.upload.known_custom_bytes",
    "renderer.frame.upload.total_bytes",
    "renderer.frame.output_count",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graphics-backend", default="native")
    parser.add_argument("--width", type=int, default=1200)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--warmup-frames", type=int, default=2)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--sets", type=int, default=2)
    parser.add_argument("--runs-per-set", type=int, default=7)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    parser.add_argument(
        "--manifest-only",
        action="store_true",
        help="Write the generated scenario manifest without executing runs.",
    )
    return parser.parse_args()


def make_scenarios() -> list[dict[str, Any]]:
    scenarios: list[dict[str, Any]] = []
    for mode, data_type, seed in (
        ("static", "bars", 42),
        ("live", "trades", 1337),
    ):
        for series_count in (1, 8, 64):
            scenarios.append(
                {
                    "id": f"{mode}-{data_type}-line-{series_count}",
                    "mode": mode,
                    "data_type": data_type,
                    "render_style": "line",
                    "series_count": series_count,
                    "seed": seed,
                    "rate": 10_000,
                }
            )
    return scenarios


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def scenario_manifest(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "status": "proposed-owner-approval-required",
        "protocol": {
            "calibration_sets": args.sets,
            "runs_per_set": args.runs_per_set,
            "warmup_frames_per_run": args.warmup_frames,
            "measured_frames_per_run": args.frames,
            "graphics_backend": args.graphics_backend,
            "finish_each_frame": True,
            "framebuffer": {"width": args.width, "height": args.height},
            "text_mode": "enabled",
        },
        "scenarios": make_scenarios(),
    }


def run_one(
    args: argparse.Namespace,
    scenario: dict[str, Any],
    set_index: int,
    run_index: int,
) -> Path:
    run_id = f"set-{set_index + 1}-run-{run_index + 1}"
    run_dir = args.output_dir / "calibration-runs" / scenario["id"] / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    existing = [
        path
        for path in sorted(run_dir.glob("*.json"))
        if path.name != "invocation.json"
    ]
    if len(existing) == 1:
        payload = json.loads(existing[0].read_text(encoding="utf-8"))
        if int(payload["metadata"]["measured_frames"]) == args.frames:
            return existing[0]
    command = [
        str(args.executable.resolve()),
        "--backend",
        "qrhi-offscreen",
        "--graphics-backend",
        args.graphics_backend,
        "--data-type",
        scenario["data_type"],
        "--render-style",
        scenario["render_style"],
        "--series-count",
        str(scenario["series_count"]),
        "--seed",
        str(scenario["seed"]),
        "--rate",
        str(scenario["rate"]),
        "--width",
        str(args.width),
        "--height",
        str(args.height),
        "--warmup-frames",
        str(args.warmup_frames),
        "--frames",
        str(args.frames),
        "--scenario",
        scenario["id"],
        "--stream",
        run_id,
        "--output-dir",
        str(run_dir.resolve()),
        "--finish",
        "--quiet",
    ]
    if scenario["mode"] == "static":
        command.append("--static")

    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=args.timeout_seconds,
        )
    except subprocess.TimeoutExpired as exc:
        (run_dir / "invocation.json").write_text(
            json.dumps(
                {
                    "command": command,
                    "timeout_seconds": args.timeout_seconds,
                    "phase": "external-timeout",
                    "stdout": exc.stdout or "",
                    "stderr": exc.stderr or "",
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        raise RuntimeError(
            f"{scenario['id']} {run_id} exceeded {args.timeout_seconds}s"
        ) from exc
    (run_dir / "invocation.json").write_text(
        json.dumps(
            {
                "command": command,
                "returncode": completed.returncode,
                "stdout": completed.stdout,
                "stderr": completed.stderr,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{scenario['id']} {run_id} failed with {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )

    artifacts = sorted(run_dir.glob("*.json"))
    artifacts = [path for path in artifacts if path.name != "invocation.json"]
    if len(artifacts) != 1:
        raise RuntimeError(
            f"{scenario['id']} {run_id} produced {len(artifacts)} raw artifacts"
        )
    artifact = artifacts[0]
    payload = json.loads(artifact.read_text(encoding="utf-8"))
    metadata = payload["metadata"]
    if metadata["actual_graphics_backend"].lower() == "null":
        raise RuntimeError(f"{scenario['id']} {run_id} unexpectedly used Null QRhi")
    if int(metadata["measured_frames"]) != args.frames:
        raise RuntimeError(f"{scenario['id']} {run_id} measured the wrong frame count")
    output_count = payload["observations"]["benchmark.frame.output_count"]["count"]
    if output_count != args.frames:
        raise RuntimeError(f"{scenario['id']} {run_id} retained the wrong output count")
    return artifact


def metric_value(observation: dict[str, Any]) -> float | None:
    value = observation.get("p50")
    if value is None:
        value = observation.get("mean")
    if isinstance(value, (int, float)) and math.isfinite(value):
        return float(value)
    return None


def median_absolute_deviation(values: list[float]) -> float:
    center = statistics.median(values)
    return statistics.median(abs(value - center) for value in values)


def propose_margin(metric: str, sets: list[list[float]]) -> dict[str, Any]:
    values = [value for calibration_set in sets for value in calibration_set]
    center = statistics.median(values)
    mad = median_absolute_deviation(values)
    set_medians = [statistics.median(calibration_set) for calibration_set in sets]
    set_drift = max(set_medians) - min(set_medians)
    scale = max(abs(center), 1.0e-12)
    deterministic = any(
        token in metric
        for token in ("count", "bytes", "checksum", "occupancy", "overwritten_samples")
    )
    relative_floor = 0.0 if deterministic else 0.05
    absolute_floor = 0.0 if deterministic else max(scale * 1.0e-9, 1.0e-9)
    relative_margin = max(
        relative_floor,
        4.0 * mad / scale,
        2.0 * set_drift / scale,
    )
    absolute_margin = max(absolute_floor, 4.0 * mad, 2.0 * set_drift)
    return {
        "center": center,
        "median_absolute_deviation": mad,
        "set_medians": set_medians,
        "set_drift": set_drift,
        "proposed_relative_margin": min(relative_margin, 1.0),
        "proposed_absolute_margin": absolute_margin,
        "sample_count": len(values),
    }


def calculate_margins(
    manifest: dict[str, Any],
    artifacts: dict[str, list[list[Path]]],
) -> dict[str, Any]:
    proposal: dict[str, Any] = {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "status": "proposed-owner-approval-required",
        "scenario_manifest": "scenario_manifest.json",
        "method": {
            "run_value": "retained per-run p50, falling back to mean",
            "relative": "max(floor, 4*MAD/center, 2*set-drift/center)",
            "absolute": "max(resolution floor, 4*MAD, 2*set-drift)",
        },
        "scenarios": {},
    }
    for scenario in manifest["scenarios"]:
        scenario_id = scenario["id"]
        loaded_sets = [
            [json.loads(path.read_text(encoding="utf-8")) for path in calibration_set]
            for calibration_set in artifacts[scenario_id]
        ]
        scenario_proposal: dict[str, Any] = {}
        for metric in DEFAULT_METRICS:
            metric_sets: list[list[float]] = []
            for calibration_set in loaded_sets:
                values = [
                    metric_value(run["observations"][metric])
                    for run in calibration_set
                    if metric in run["observations"]
                ]
                retained = [value for value in values if value is not None]
                if retained:
                    metric_sets.append(retained)
            if len(metric_sets) == len(loaded_sets):
                scenario_proposal[metric] = propose_margin(metric, metric_sets)
        proposal["scenarios"][scenario_id] = scenario_proposal
    return proposal


def main() -> int:
    args = parse_args()
    if args.sets < 2 or args.runs_per_set < 2:
        raise ValueError("calibration requires at least two sets and two runs per set")
    if args.frames < 1 or args.warmup_frames < 0:
        raise ValueError("frame counts are invalid")
    if not args.manifest_only and not args.executable.is_file():
        raise FileNotFoundError(args.executable)

    manifest = scenario_manifest(args)
    write_json(args.output_dir / "scenario_manifest.json", manifest)
    if args.manifest_only:
        return 0

    artifacts: dict[str, list[list[Path]]] = {}
    for scenario in manifest["scenarios"]:
        scenario_sets: list[list[Path]] = []
        for set_index in range(args.sets):
            calibration_set = [
                run_one(args, scenario, set_index, run_index)
                for run_index in range(args.runs_per_set)
            ]
            scenario_sets.append(calibration_set)
        artifacts[scenario["id"]] = scenario_sets

    proposal = calculate_margins(manifest, artifacts)
    write_json(args.output_dir / "proposed_noise_margins.json", proposal)
    print(args.output_dir.resolve())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI must report the failed phase.
        print(f"calibration failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
