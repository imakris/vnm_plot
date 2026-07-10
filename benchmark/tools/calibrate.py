#!/usr/bin/env python3
"""Run the retained vnm_plot calibration protocol and propose noise rules."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import statistics
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


METRIC_POLICIES: dict[str, dict[str, Any]] = {
    "benchmark.frame.total_ms": {"reducers": ("p50", "p95", "p99"), "unit": "ms"},
    "benchmark.frame.submission_ms": {"reducers": ("p50", "p95", "p99"), "unit": "ms"},
    "benchmark.frame.gpu_finish_ms": {"reducers": ("p50", "p95", "p99"), "unit": "ms"},
    "benchmark.planning.time_ms": {"reducers": ("p50", "p95", "p99"), "unit": "ms"},
    "benchmark.snapshot.time_ns": {"reducers": ("p50", "p95", "p99"), "unit": "ns"},
    "benchmark.snapshot.count": {"reducers": ("total",), "unit": "count"},
    "benchmark.snapshot.bytes": {"reducers": ("total",), "unit": "bytes"},
    "benchmark.producer.lock_wait_ns": {"reducers": ("mean",), "unit": "ns"},
    "benchmark.ring.published_samples": {"reducers": ("total",), "unit": "count"},
    "benchmark.ring.published_samples_per_second": {
        "reducers": ("mean",),
        "unit": "rate",
        "direction": "higher",
    },
    "benchmark.ring.overwritten_samples": {
        "reducers": ("total",),
        "unit": "count",
        "deterministic_zero": True,
    },
    "benchmark.memory.process_high_water_bytes": {"reducers": ("max",), "unit": "bytes"},
    "renderer.series_window.monotonicity_scan_count": {
        "reducers": ("total",),
        "unit": "count",
        "deterministic_zero": True,
    },
    "renderer.series_window.monotonicity_scan_samples": {
        "reducers": ("total",),
        "unit": "count",
        "deterministic_zero": True,
    },
    "renderer.frame.buffer_allocation_count": {
        "reducers": ("total",),
        "unit": "count",
        "deterministic_zero": True,
    },
    "renderer.frame.buffer_allocation_bytes": {
        "reducers": ("total",),
        "unit": "bytes",
        "deterministic_zero": True,
    },
    "renderer.frame.upload.primary_bytes": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "bytes",
    },
    "renderer.frame.upload.primary_count": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "count",
    },
    "renderer.frame.upload.line_window_bytes": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "bytes",
    },
    "renderer.frame.upload.line_window_count": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "count",
    },
    "renderer.frame.upload.uniform_bytes": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "bytes",
    },
    "renderer.frame.upload.uniform_count": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "count",
    },
    "renderer.frame.upload.known_custom_bytes": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "bytes",
        "deterministic_zero": True,
    },
    "renderer.frame.upload.known_custom_count": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "count",
        "deterministic_zero": True,
    },
    "renderer.frame.upload.total_bytes": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "bytes",
    },
    "renderer.frame.upload.total_count": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "count",
    },
    "benchmark.frame.output_count": {
        "reducers": ("total",),
        "unit": "count",
        "exact_expected": True,
    },
}

FINGERPRINT_FIELDS = (
    "actual_graphics_backend",
    "build_source_commit",
    "build_type",
    "cmake_version",
    "compiler",
    "dependency_commit",
    "device_id",
    "device_name",
    "driver_identity",
    "executable_sha256",
    "framebuffer",
    "qt_version",
    "source_commit",
    "source_diff_sha256",
    "source_tree",
    "vendor_id",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graphics-backend", default="native")
    parser.add_argument("--width", type=int, default=1200)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--warmup-runs", type=int, default=2)
    parser.add_argument("--warmup-frames", type=int, default=2)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--sets", type=int, default=2)
    parser.add_argument("--runs-per-set", type=int, default=7)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    parser.add_argument("--unstable-drift-threshold", type=float, default=0.25)
    parser.add_argument(
        "--manifest-only",
        action="store_true",
        help="Write the generated scenario manifest without executing runs.",
    )
    parser.add_argument(
        "--allow-dirty-source",
        action="store_true",
        help="Permit calibration artifacts built from a dirty source tree.",
    )
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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


def write_immutable_json(path: Path, value: dict[str, Any]) -> dict[str, Any]:
    """Create a manifest once, or validate an equivalent retained manifest."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        retained = json.loads(path.read_text(encoding="utf-8"))
        retained_comparison = dict(retained)
        value_comparison = dict(value)
        retained_comparison.pop("generated_at_utc", None)
        value_comparison.pop("generated_at_utc", None)
        if retained_comparison != value_comparison:
            raise RuntimeError(f"refusing to replace incompatible retained manifest: {path}")
        return retained
    with path.open("x", encoding="utf-8") as output:
        output.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
    return value


def scenario_manifest(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "schema_version": 2,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "status": "CALIBRATION_REVIEW_REQUIRED",
        "runner": {
            "path": str(Path(__file__).resolve()),
            "sha256": sha256_file(Path(__file__).resolve()),
            "python": sys.version,
            "platform": platform.platform(),
        },
        "protocol": {
            "calibration_sets": args.sets,
            "warmup_runs_per_set": args.warmup_runs,
            "measured_runs_per_set": args.runs_per_set,
            "runner_warmup_frames": args.warmup_frames,
            "measured_frames_per_run": args.frames,
            "graphics_backend": args.graphics_backend,
            "finish_each_frame": True,
            "framebuffer": {"width": args.width, "height": args.height},
            "text_mode": "enabled",
            "outlier_policy": "retain-all",
            "stability_review_threshold": args.unstable_drift_threshold,
        },
        "scenarios": make_scenarios(),
    }


def validate_artifact(
    args: argparse.Namespace,
    scenario: dict[str, Any],
    artifact: Path,
    run_id: str,
    expected_command: list[str],
) -> dict[str, Any]:
    payload = json.loads(artifact.read_text(encoding="utf-8"))
    metadata = payload["metadata"]
    if metadata["actual_graphics_backend"].lower() == "null":
        raise RuntimeError(f"{scenario['id']} unexpectedly used Null QRhi")
    expectations = {
        "data_type": scenario["data_type"].capitalize(),
        "finish_state": "enabled",
        "framebuffer": f"{args.width}x{args.height}",
        "presentation_backend": "qrhi-offscreen",
        "rate": f"{scenario['rate']:.6f}",
        "render_style": scenario["render_style"].capitalize(),
        "requested_graphics_backend": args.graphics_backend,
        "scenario": scenario["id"],
        "seed": str(scenario["seed"]),
        "series_count": str(scenario["series_count"]),
        "show_text": "true",
        "static_data": "true" if scenario["mode"] == "static" else "false",
        "stream": run_id,
        "warmup_frames": str(args.warmup_frames),
    }
    for name, expected in expectations.items():
        if metadata.get(name) != expected:
            raise RuntimeError(
                f"{scenario['id']} metadata {name}={metadata.get(name)!r}, "
                f"expected {expected!r}"
            )
    if not args.allow_dirty_source and metadata.get("source_dirty") != "false":
        raise RuntimeError(f"{scenario['id']} was built from a dirty source tree")
    if metadata.get("build_source_commit") != metadata.get("source_commit"):
        raise RuntimeError(f"{scenario['id']} binary/source commit mismatch")
    if metadata.get("executable_sha256") != sha256_file(args.executable):
        raise RuntimeError(f"{scenario['id']} executable fingerprint mismatch")
    if payload.get("invocation_args") != expected_command:
        raise RuntimeError(f"{scenario['id']} structured invocation mismatch")
    if int(metadata["measured_frames"]) != args.frames:
        raise RuntimeError(f"{scenario['id']} measured the wrong frame count")
    observations = payload["observations"]
    output_count = observations["benchmark.frame.output_count"]["total"]
    if output_count != args.frames:
        raise RuntimeError(f"{scenario['id']} retained the wrong output count")
    missing = set(METRIC_POLICIES) - observations.keys()
    if missing:
        raise RuntimeError(
            f"{scenario['id']} is missing required observations: {sorted(missing)}"
        )
    missing_fingerprint = [field for field in FINGERPRINT_FIELDS if not metadata.get(field)]
    if missing_fingerprint:
        raise RuntimeError(
            f"{scenario['id']} is missing fingerprint fields: {missing_fingerprint}"
        )
    return payload


def attempt_identity() -> str:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    return f"attempt-{timestamp}-{uuid.uuid4().hex[:8]}"


def retained_success(
    args: argparse.Namespace,
    scenario: dict[str, Any],
    run_root: Path,
    run_id: str,
    expected_fingerprint: dict[str, Any],
) -> Path | None:
    for attempt in sorted(run_root.glob("attempt-*"), reverse=True):
        artifacts = sorted(attempt.glob("inspector_benchmark_*.json"))
        if len(artifacts) != 1:
            continue
        try:
            expected_command = make_command(args, scenario, run_id, attempt)
            invocation = json.loads(
                (attempt / "invocation.json").read_text(encoding="utf-8")
            )
            if invocation.get("fingerprint") != expected_fingerprint:
                continue
            if invocation.get("command") != expected_command:
                continue
            validate_artifact(args, scenario, artifacts[0], run_id, expected_command)
        except (KeyError, TypeError, ValueError, RuntimeError):
            continue
        return artifacts[0]
    return None


def run_fingerprint(
    args: argparse.Namespace,
    scenario: dict[str, Any],
    run_id: str,
) -> dict[str, Any]:
    return {
        "runner_sha256": sha256_file(Path(__file__).resolve()),
        "executable_sha256": sha256_file(args.executable.resolve()),
        "graphics_backend": args.graphics_backend,
        "width": args.width,
        "height": args.height,
        "warmup_frames": args.warmup_frames,
        "measured_frames": args.frames,
        "scenario": scenario,
        "run_id": run_id,
    }


def make_command(
    args: argparse.Namespace,
    scenario: dict[str, Any],
    run_id: str,
    attempt_dir: Path,
) -> list[str]:
    command = [
        str(args.executable.resolve()),
        "--backend", "qrhi-offscreen",
        "--graphics-backend", args.graphics_backend,
        "--data-type", scenario["data_type"],
        "--render-style", scenario["render_style"],
        "--series-count", str(scenario["series_count"]),
        "--seed", str(scenario["seed"]),
        "--rate", str(scenario["rate"]),
        "--width", str(args.width),
        "--height", str(args.height),
        "--warmup-frames", str(args.warmup_frames),
        "--frames", str(args.frames),
        "--scenario", scenario["id"],
        "--stream", run_id,
        "--output-dir", str(attempt_dir.resolve()),
        "--finish",
        "--quiet",
    ]
    if scenario["mode"] == "static":
        command.append("--static")
    return command


def run_one(
    args: argparse.Namespace,
    scenario: dict[str, Any],
    set_index: int,
    run_kind: str,
    run_index: int,
) -> Path:
    run_id = f"set-{set_index + 1}-{run_kind}-{run_index + 1}"
    run_root = args.output_dir / "calibration-runs" / scenario["id"] / run_id
    run_root.mkdir(parents=True, exist_ok=True)
    previous_attempts = sorted(run_root.glob("attempt-*"))
    fingerprint = run_fingerprint(args, scenario, run_id)
    existing = retained_success(
        args,
        scenario,
        run_root,
        run_id,
        fingerprint,
    )
    if existing:
        return existing

    attempt_dir = run_root / attempt_identity()
    command = make_command(args, scenario, run_id, attempt_dir)
    attempt_dir.mkdir(parents=True, exist_ok=False)
    recovery_of = previous_attempts[-1].name if previous_attempts else None

    started_at = datetime.now(timezone.utc).isoformat()
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=args.timeout_seconds,
        )
        invocation = {
            "schema_version": 1,
            "attempt": attempt_dir.name,
            "recovery_of": recovery_of,
            "started_at_utc": started_at,
            "ended_at_utc": datetime.now(timezone.utc).isoformat(),
            "command": command,
            "fingerprint": fingerprint,
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }
    except subprocess.TimeoutExpired as exc:
        invocation = {
            "schema_version": 1,
            "attempt": attempt_dir.name,
            "recovery_of": recovery_of,
            "started_at_utc": started_at,
            "ended_at_utc": datetime.now(timezone.utc).isoformat(),
            "command": command,
            "fingerprint": fingerprint,
            "timeout_seconds": args.timeout_seconds,
            "phase": "external-timeout",
            "stdout": exc.stdout or "",
            "stderr": exc.stderr or "",
        }
        write_json(attempt_dir / "invocation.json", invocation)
        raise RuntimeError(
            f"{scenario['id']} {run_id} exceeded {args.timeout_seconds}s; "
            f"retained {attempt_dir}"
        ) from exc

    write_json(attempt_dir / "invocation.json", invocation)
    if completed.returncode != 0:
        raise RuntimeError(
            f"{scenario['id']} {run_id} failed with {completed.returncode}; "
            f"retained {attempt_dir}"
        )
    artifacts = sorted(attempt_dir.glob("inspector_benchmark_*.json"))
    if len(artifacts) != 1:
        raise RuntimeError(
            f"{scenario['id']} {run_id} produced {len(artifacts)} raw artifacts; "
            f"retained {attempt_dir}"
        )
    validate_artifact(args, scenario, artifacts[0], run_id, command)
    return artifacts[0]


def reducer_value(observation: dict[str, Any], reducer: str) -> float | None:
    value = observation.get(reducer)
    if isinstance(value, (int, float)) and math.isfinite(value):
        return float(value)
    return None


def smallest_positive_spacing(values: list[float]) -> float | None:
    unique = sorted(set(values))
    spacings = [right - left for left, right in zip(unique, unique[1:]) if right > left]
    return min(spacings) if spacings else None


def measurement_resolution(
    policy: dict[str, Any],
    runs: list[dict[str, Any]],
    values: list[float],
) -> float:
    unit = policy["unit"]
    if unit in {"count", "bytes"}:
        return 1.0
    clock_resolutions = [
        float(run["observations"]["benchmark.clock.steady_resolution_ns"]["mean"])
        for run in runs
    ]
    clock_resolution_ns = max(clock_resolutions)
    if unit == "ns":
        return clock_resolution_ns
    if unit == "ms":
        return clock_resolution_ns / 1_000_000.0
    spacing = smallest_positive_spacing(values)
    return spacing if spacing is not None else max(abs(statistics.median(values)) * 1.0e-12, 1.0e-12)


def propose_rule(
    policy: dict[str, Any],
    calibration_sets: list[list[float]],
    resolution: float,
    unstable_threshold: float,
) -> dict[str, Any]:
    set_medians = [statistics.median(values) for values in calibration_sets]
    center = statistics.median(set_medians)
    absolute_drift = abs(set_medians[1] - set_medians[0])
    result: dict[str, Any] = {
        "center": center,
        "set_medians": set_medians,
        "absolute_median_drift": absolute_drift,
        "measurement_resolution": resolution,
        "direction": policy.get("direction", "lower"),
        "status": "PROPOSED",
    }
    if policy.get("exact_expected"):
        result.update({"rule": "exact", "expected": center, "relative_margin": None})
        return result
    if policy.get("deterministic_zero") and set_medians == [0.0, 0.0]:
        result.update({"rule": "exact_zero", "absolute_margin": 0.0, "relative_margin": None})
        return result
    if min(set_medians) <= 0.0 or center <= resolution:
        result.update(
            {
                "rule": "absolute",
                "absolute_margin": max(resolution, absolute_drift),
                "relative_margin": None,
                "status": "CALIBRATION_REVIEW_REQUIRED",
                "reason": "nonpositive-or-sub-resolution-median",
            }
        )
        return result

    relative_drift = max(
        absolute_drift / abs(set_medians[0]),
        absolute_drift / abs(set_medians[1]),
    )
    result.update(
        {
            "rule": "relative",
            "relative_margin": relative_drift,
            "absolute_margin": absolute_drift,
        }
    )
    if relative_drift > unstable_threshold:
        result.update(
            {
                "status": "CALIBRATION_REVIEW_REQUIRED",
                "reason": "relative-median-drift-exceeds-stability-threshold",
            }
        )
    return result


def calculate_margins(
    args: argparse.Namespace,
    manifest: dict[str, Any],
    artifacts: dict[str, list[list[Path]]],
) -> dict[str, Any]:
    proposal: dict[str, Any] = {
        "schema_version": 2,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "status": "CALIBRATION_REVIEW_REQUIRED",
        "scenario_manifest": "scenario_manifest.json",
        "method": {
            "set_statistic": "median of seven retained run values",
            "relative_margin": "maximum relative median drift between the two sets",
            "zero_rule": "designated deterministic zero counters require exact zero",
            "sub_resolution_rule": "relative unavailable; propose resolution-based absolute margin",
            "unstable_threshold": args.unstable_drift_threshold,
            "outliers": "retained; no automatic removal",
        },
        "scenarios": {},
    }
    fingerprint: dict[str, set[str]] = {field: set() for field in FINGERPRINT_FIELDS}
    review_required: list[str] = []
    for scenario in manifest["scenarios"]:
        scenario_id = scenario["id"]
        loaded_sets = [
            [json.loads(path.read_text(encoding="utf-8")) for path in calibration_set]
            for calibration_set in artifacts[scenario_id]
        ]
        flat_runs = [run for calibration_set in loaded_sets for run in calibration_set]
        for run in flat_runs:
            for field in FINGERPRINT_FIELDS:
                fingerprint[field].add(str(run["metadata"][field]))
        scenario_proposal: dict[str, Any] = {}
        for metric, policy in METRIC_POLICIES.items():
            metric_proposal: dict[str, Any] = {}
            for reducer in policy["reducers"]:
                calibration_sets: list[list[float]] = []
                for calibration_set in loaded_sets:
                    values = [
                        reducer_value(run["observations"][metric], reducer)
                        for run in calibration_set
                    ]
                    retained = [value for value in values if value is not None]
                    if len(retained) != args.runs_per_set:
                        raise RuntimeError(
                            f"{scenario_id}/{metric}/{reducer} has incomplete run values"
                        )
                    calibration_sets.append(retained)
                all_values = [value for values in calibration_sets for value in values]
                resolution = measurement_resolution(policy, flat_runs, all_values)
                rule = propose_rule(
                    policy,
                    calibration_sets,
                    resolution,
                    args.unstable_drift_threshold,
                )
                if rule["status"] == "CALIBRATION_REVIEW_REQUIRED":
                    review_required.append(f"{scenario_id}/{metric}/{reducer}")
                metric_proposal[reducer] = rule
            scenario_proposal[metric] = metric_proposal
        proposal["scenarios"][scenario_id] = scenario_proposal

    mixed = {field: sorted(values) for field, values in fingerprint.items() if len(values) != 1}
    if mixed:
        raise RuntimeError(f"calibration artifacts mix fingerprint fields: {mixed}")
    proposal["fingerprint"] = {
        field: next(iter(values)) for field, values in fingerprint.items()
    }
    proposal["review_required_rules"] = review_required
    return proposal


def main() -> int:
    args = parse_args()
    if args.sets != 2 or args.runs_per_set != 7 or args.warmup_runs != 2:
        raise ValueError("the retained protocol requires two sets, two warm-up runs, and seven measured runs")
    if args.frames < 1 or args.warmup_frames < 0:
        raise ValueError("frame counts are invalid")
    if not args.manifest_only and not args.executable.is_file():
        raise FileNotFoundError(args.executable)

    manifest = scenario_manifest(args)
    manifest = write_immutable_json(args.output_dir / "scenario_manifest.json", manifest)
    if args.manifest_only:
        return 0

    artifacts: dict[str, list[list[Path]]] = {}
    for scenario in manifest["scenarios"]:
        measured_sets: list[list[Path]] = []
        for set_index in range(args.sets):
            for run_index in range(args.warmup_runs):
                run_one(args, scenario, set_index, "warmup", run_index)
            measured_sets.append(
                [
                    run_one(args, scenario, set_index, "measured", run_index)
                    for run_index in range(args.runs_per_set)
                ]
            )
        artifacts[scenario["id"]] = measured_sets

    proposal = calculate_margins(args, manifest, artifacts)
    write_immutable_json(args.output_dir / "proposed_noise_margins.json", proposal)
    print(args.output_dir.resolve())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI must retain/report failed phases.
        print(f"calibration failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
