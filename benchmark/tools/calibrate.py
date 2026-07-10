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

from phase_trace import expected_phases, parse_and_validate_phase_trace


FIXED_WIDTH = 1200
FIXED_HEIGHT = 720
FIXED_WARMUP_RUNS = 2
FIXED_WARMUP_FRAMES = 2
FIXED_MEASURED_FRAMES = 120
FIXED_SETS = 2
FIXED_RUNS_PER_SET = 7
FIXED_UNSTABLE_DRIFT_THRESHOLD = 0.25
FIXED_STATIC_SAMPLE_COUNT = 10_000
FIXED_LIVE_RATE = 1_000


METRIC_POLICIES: dict[str, dict[str, Any]] = {
    "benchmark.frame.total_ms": {"reducers": ("p50", "p95", "p99"), "unit": "ms"},
    "benchmark.frame.submission_ms": {"reducers": ("p50", "p95", "p99"), "unit": "ms"},
    "benchmark.frame.gpu_finish_ms": {"reducers": ("p50", "p95", "p99"), "unit": "ms"},
    "benchmark.planning.time_ms": {"reducers": ("p50", "p95", "p99"), "unit": "ms"},
    "benchmark.snapshot.time_ns": {"reducers": ("p50", "p95", "p99"), "unit": "ns"},
    "benchmark.snapshot.count": {"reducers": ("total",), "unit": "count"},
    "benchmark.snapshot.view_bytes": {"reducers": ("total",), "unit": "bytes"},
    "benchmark.snapshot.copied_bytes": {
        "reducers": ("total",),
        "unit": "bytes",
        "deterministic_zero": True,
    },
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
    },
    "renderer.series_window.monotonicity_scan_samples": {
        "reducers": ("total",),
        "unit": "count",
    },
    "renderer.frame.gpu_buffer_allocation_count": {
        "reducers": ("total",),
        "unit": "count",
    },
    "renderer.frame.gpu_buffer_allocation_bytes": {
        "reducers": ("total",),
        "unit": "bytes",
    },
    "benchmark.frame.cpu_allocation_count": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "count",
    },
    "benchmark.frame.cpu_allocation_bytes": {
        "reducers": ("p50", "p95", "p99"),
        "unit": "bytes",
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

STATIC_DETERMINISTIC_ZERO_METRICS = frozenset(
    {
        "benchmark.ring.published_samples",
        "benchmark.ring.published_samples_per_second",
        "renderer.series_window.monotonicity_scan_count",
        "renderer.series_window.monotonicity_scan_samples",
        "renderer.frame.gpu_buffer_allocation_count",
        "renderer.frame.gpu_buffer_allocation_bytes",
    }
)

FIXED_CONTEXT_PROFILE_REQUEST = "core"
FIXED_CONTEXT_SAMPLE_COUNT = 1
FIXED_CONTEXT_VERSION_REQUEST = "3.3"
FIXED_MEASURED_RENDER_SAMPLE_COUNT = 4

FINGERPRINT_FIELDS = (
    "actual_graphics_backend",
    "build_cpu_architecture",
    "build_dependency_commit",
    "build_dependency_dirty",
    "build_qt_version",
    "build_source_commit",
    "build_source_diff_sha256",
    "build_source_dirty",
    "build_source_tree",
    "build_type",
    "cmake_version",
    "compiler",
    "context_profile_request",
    "context_sample_count",
    "context_version_request",
    "dependency_commit",
    "dependency_dirty",
    "device_id",
    "device_name",
    "driver_identity",
    "driver_version",
    "env.GALLIUM_DRIVER",
    "env.LP_NUM_THREADS",
    "executable_sha256",
    "framebuffer",
    "kernel_type",
    "kernel_version",
    "machine_id_sha256",
    "os",
    "qt_version",
    "sample_count",
    "source_commit",
    "source_diff_sha256",
    "source_git_tree",
    "source_tree",
    "vendor_id",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--graphics-backend", default="native")
    parser.add_argument("--width", type=int, default=FIXED_WIDTH)
    parser.add_argument("--height", type=int, default=FIXED_HEIGHT)
    parser.add_argument("--warmup-runs", type=int, default=FIXED_WARMUP_RUNS)
    parser.add_argument("--warmup-frames", type=int, default=FIXED_WARMUP_FRAMES)
    parser.add_argument("--frames", type=int, default=FIXED_MEASURED_FRAMES)
    parser.add_argument("--sets", type=int, default=FIXED_SETS)
    parser.add_argument("--runs-per-set", type=int, default=FIXED_RUNS_PER_SET)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    parser.add_argument(
        "--unstable-drift-threshold",
        type=float,
        default=FIXED_UNSTABLE_DRIFT_THRESHOLD,
    )
    parser.add_argument(
        "--manifest-only",
        action="store_true",
        help="Write the generated scenario manifest without executing runs.",
    )
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def captured_text(value: object) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value or "")


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
                    "rate": FIXED_LIVE_RATE,
                }
            )
    return scenarios


def policy_for_scenario(
    scenario: dict[str, Any],
    metric: str,
) -> dict[str, Any]:
    policy = dict(METRIC_POLICIES[metric])
    if scenario["mode"] == "static" and metric in STATIC_DETERMINISTIC_ZERO_METRICS:
        policy["deterministic_zero"] = True
    return policy


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
        "schema_version": 3,
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
            "static_samples_per_series": FIXED_STATIC_SAMPLE_COUNT,
            "context_profile_request": FIXED_CONTEXT_PROFILE_REQUEST,
            "context_sample_count": FIXED_CONTEXT_SAMPLE_COUNT,
            "context_version_request": FIXED_CONTEXT_VERSION_REQUEST,
            "measured_render_sample_count": FIXED_MEASURED_RENDER_SAMPLE_COUNT,
            "text_mode": "enabled",
            "outlier_policy": "retain-all",
            "stability_review_threshold": args.unstable_drift_threshold,
        },
        "scenarios": make_scenarios(),
    }


def validate_fixed_render_protocol(metadata: dict[str, Any], scenario_id: str) -> None:
    expectations = {
        "context_profile_request": FIXED_CONTEXT_PROFILE_REQUEST,
        "context_sample_count": str(FIXED_CONTEXT_SAMPLE_COUNT),
        "context_version_request": FIXED_CONTEXT_VERSION_REQUEST,
        "sample_count": str(FIXED_MEASURED_RENDER_SAMPLE_COUNT),
    }
    for name, expected in expectations.items():
        if metadata.get(name) != expected:
            raise RuntimeError(
                f"{scenario_id} metadata {name}={metadata.get(name)!r}, "
                f"expected {expected!r}"
            )


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
    validate_fixed_render_protocol(metadata, scenario["id"])
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
        "static_sample_count": str(FIXED_STATIC_SAMPLE_COUNT),
        "stream": run_id,
        "warmup_frames": str(args.warmup_frames),
    }
    for name, expected in expectations.items():
        if metadata.get(name) != expected:
            raise RuntimeError(
                f"{scenario['id']} metadata {name}={metadata.get(name)!r}, "
                f"expected {expected!r}"
            )
    validate_build_runtime_identity(args, metadata, scenario["id"])
    if metadata.get("executable_sha256") != sha256_file(args.executable):
        raise RuntimeError(f"{scenario['id']} executable fingerprint mismatch")
    if payload.get("invocation_args") != expected_command:
        raise RuntimeError(f"{scenario['id']} structured invocation mismatch")
    if int(metadata["measured_frames"]) != args.frames:
        raise RuntimeError(f"{scenario['id']} measured the wrong frame count")
    validate_phase_trace(metadata, artifact.parent)
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
    expected_environment = getattr(args, "environment_fingerprint", None)
    if expected_environment:
        mismatches = {
            field: {"expected": expected, "actual": metadata.get(field)}
            for field, expected in expected_environment.items()
            if str(metadata.get(field)) != str(expected)
        }
        if mismatches:
            raise RuntimeError(
                f"{scenario['id']} current environment fingerprint mismatch: {mismatches}"
            )
    return payload


def validate_build_runtime_identity(
    args: argparse.Namespace,
    metadata: dict[str, Any],
    label: str,
) -> None:
    if metadata.get("source_dirty") != "false":
        raise RuntimeError(f"{label} was run from a dirty source tree")
    if metadata.get("build_source_dirty") != "false":
        raise RuntimeError(f"{label} binary was configured from a dirty source tree")
    identity_pairs = (
        ("build_source_commit", "source_commit", "source commit"),
        ("build_source_diff_sha256", "source_diff_sha256", "source diff"),
        ("build_source_tree", "source_git_tree", "source git tree"),
        ("build_dependency_commit", "dependency_commit", "dependency commit"),
        ("build_qt_version", "qt_version", "Qt version"),
    )
    for build_field, runtime_field, description in identity_pairs:
        if metadata.get(build_field) != metadata.get(runtime_field):
            raise RuntimeError(f"{label} build/runtime {description} mismatch")
    if metadata.get("build_dependency_dirty") != "false" or metadata.get("dependency_dirty") != "false":
        raise RuntimeError(f"{label} dependency tree is dirty")


def validate_phase_trace(metadata: dict[str, Any], attempt_dir: Path) -> Path:
    trace_value = metadata.get("phase_trace_path")
    if not trace_value:
        raise RuntimeError("phase trace path is missing")
    trace_path = Path(trace_value)
    if not trace_path.is_absolute():
        trace_path = attempt_dir / trace_path
    trace_path = trace_path.resolve()
    if trace_path.parent != attempt_dir.resolve():
        raise RuntimeError("phase trace escapes its retained attempt")
    if not trace_path.is_file():
        raise RuntimeError("phase trace is missing")
    try:
        warmup_frames = int(metadata["warmup_frames"])
        measured_frames = int(metadata["measured_frames"])
    except (KeyError, TypeError, ValueError) as exc:
        raise RuntimeError("phase trace frame-count metadata is invalid") from exc
    parse_and_validate_phase_trace(
        trace_path.read_text(encoding="utf-8"),
        warmup_frames,
        measured_frames,
    )
    return trace_path


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
            validation = json.loads(
                (attempt / "validation.json").read_text(encoding="utf-8")
            )
            if validation.get("status") != "PASS":
                continue
            if validation.get("artifact") != artifacts[0].name or validation.get(
                "artifact_sha256"
            ) != sha256_file(artifacts[0]):
                continue
            trace_relative = Path(validation.get("phase_trace", ""))
            trace_path = (attempt / trace_relative).resolve()
            if trace_path.parent != attempt.resolve() or not trace_path.is_file():
                continue
            if validation.get("phase_trace_sha256") != sha256_file(trace_path):
                continue
            expected_command = make_command(args, scenario, run_id, attempt)
            invocation = json.loads(
                (attempt / "invocation.json").read_text(encoding="utf-8")
            )
            if invocation.get("fingerprint") != expected_fingerprint:
                continue
            if invocation.get("command") != expected_command:
                continue
            payload = validate_artifact(
                args,
                scenario,
                artifacts[0],
                run_id,
                expected_command,
            )
            if validate_phase_trace(payload["metadata"], attempt) != trace_path:
                continue
        except (KeyError, OSError, TypeError, ValueError, RuntimeError):
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
        "environment_fingerprint": getattr(args, "environment_fingerprint", {}),
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
        "--static-samples", str(FIXED_STATIC_SAMPLE_COUNT),
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


def make_environment_probe_command(
    args: argparse.Namespace,
    attempt_dir: Path,
) -> list[str]:
    return [
        str(args.executable.resolve()),
        "--backend", "qrhi-offscreen",
        "--graphics-backend", "native",
        "--static",
        "--data-type", "bars",
        "--render-style", "line",
        "--series-count", "1",
        "--seed", "42",
        "--rate", "10000",
        "--static-samples", str(FIXED_STATIC_SAMPLE_COUNT),
        "--width", str(FIXED_WIDTH),
        "--height", str(FIXED_HEIGHT),
        "--warmup-frames", str(FIXED_WARMUP_FRAMES),
        "--frames", "1",
        "--scenario", "calibration-environment-probe",
        "--stream", "environment-probe",
        "--output-dir", str(attempt_dir.resolve()),
        "--finish",
        "--quiet",
    ]


def run_environment_probe(args: argparse.Namespace) -> dict[str, str]:
    probe_root = args.output_dir / "environment-probes"
    probe_root.mkdir(parents=True, exist_ok=True)
    previous_attempts = sorted(probe_root.glob("attempt-*"))
    attempt_dir = probe_root / attempt_identity()
    attempt_dir.mkdir(parents=True, exist_ok=False)
    command = make_environment_probe_command(args, attempt_dir)
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
            "recovery_of": previous_attempts[-1].name if previous_attempts else None,
            "started_at_utc": started_at,
            "ended_at_utc": datetime.now(timezone.utc).isoformat(),
            "command": command,
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        }
    except subprocess.TimeoutExpired as exc:
        invocation = {
            "schema_version": 1,
            "attempt": attempt_dir.name,
            "recovery_of": previous_attempts[-1].name if previous_attempts else None,
            "started_at_utc": started_at,
            "ended_at_utc": datetime.now(timezone.utc).isoformat(),
            "command": command,
            "timeout_seconds": args.timeout_seconds,
            "stdout": captured_text(exc.stdout),
            "stderr": captured_text(exc.stderr),
        }
        write_json(attempt_dir / "invocation.json", invocation)
        write_json(attempt_dir / "validation.json", {"status": "FAIL", "reason": "timeout"})
        raise RuntimeError(f"environment probe timed out; retained {attempt_dir}") from exc

    write_json(attempt_dir / "invocation.json", invocation)
    try:
        if completed.returncode != 0:
            raise RuntimeError(f"benchmark exited with {completed.returncode}")
        artifacts = sorted(attempt_dir.glob("inspector_benchmark_*.json"))
        if len(artifacts) != 1:
            raise RuntimeError(f"expected one environment artifact, found {len(artifacts)}")
        payload = json.loads(artifacts[0].read_text(encoding="utf-8"))
        metadata = payload["metadata"]
        if payload.get("invocation_args") != command:
            raise RuntimeError("environment probe structured invocation mismatch")
        if metadata.get("requested_graphics_backend") != "native":
            raise RuntimeError("environment probe did not request native QRhi")
        if metadata.get("actual_graphics_backend", "").lower() in {"", "null", "uninitialized"}:
            raise RuntimeError("environment probe selected an unusable QRhi backend")
        if metadata.get("scenario") != "calibration-environment-probe":
            raise RuntimeError("environment probe scenario mismatch")
        if int(metadata.get("measured_frames", 0)) != 1:
            raise RuntimeError("environment probe frame count mismatch")
        if metadata.get("executable_sha256") != sha256_file(args.executable):
            raise RuntimeError("environment probe executable fingerprint mismatch")
        validate_build_runtime_identity(args, metadata, "environment probe")
        trace_path = validate_phase_trace(metadata, attempt_dir)
        missing = [field for field in FINGERPRINT_FIELDS if not metadata.get(field)]
        if missing:
            raise RuntimeError(f"environment probe is missing fingerprint fields: {missing}")
        fingerprint = {field: str(metadata[field]) for field in FINGERPRINT_FIELDS}
    except Exception as exc:
        write_json(attempt_dir / "validation.json", {"status": "FAIL", "reason": str(exc)})
        raise

    write_json(
        attempt_dir / "validation.json",
        {
            "status": "PASS",
            "artifact": artifacts[0].name,
            "artifact_sha256": sha256_file(artifacts[0]),
            "phase_trace": trace_path.name,
            "phase_trace_sha256": sha256_file(trace_path),
            "fingerprint": fingerprint,
        },
    )
    return fingerprint


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
            "stdout": captured_text(exc.stdout),
            "stderr": captured_text(exc.stderr),
        }
        write_json(attempt_dir / "invocation.json", invocation)
        write_json(
            attempt_dir / "validation.json",
            {"status": "FAIL", "reason": "external-timeout"},
        )
        raise RuntimeError(
            f"{scenario['id']} {run_id} exceeded {args.timeout_seconds}s; "
            f"retained {attempt_dir}"
        ) from exc

    write_json(attempt_dir / "invocation.json", invocation)
    if completed.returncode != 0:
        write_json(
            attempt_dir / "validation.json",
            {
                "status": "FAIL",
                "reason": "benchmark-process-failed",
                "returncode": completed.returncode,
            },
        )
        raise RuntimeError(
            f"{scenario['id']} {run_id} failed with {completed.returncode}; "
            f"retained {attempt_dir}"
        )
    artifacts = sorted(attempt_dir.glob("inspector_benchmark_*.json"))
    if len(artifacts) != 1:
        write_json(
            attempt_dir / "validation.json",
            {
                "status": "FAIL",
                "reason": "missing-or-ambiguous-raw-artifact",
                "artifact_count": len(artifacts),
            },
        )
        raise RuntimeError(
            f"{scenario['id']} {run_id} produced {len(artifacts)} raw artifacts; "
            f"retained {attempt_dir}"
        )
    try:
        payload = validate_artifact(args, scenario, artifacts[0], run_id, command)
        trace_path = validate_phase_trace(payload["metadata"], attempt_dir)
    except Exception as exc:
        write_json(
            attempt_dir / "validation.json",
            {"status": "FAIL", "reason": str(exc)},
        )
        raise
    write_json(
        attempt_dir / "validation.json",
        {
            "status": "PASS",
            "artifact": artifacts[0].name,
            "artifact_sha256": sha256_file(artifacts[0]),
            "phase_trace": trace_path.name,
            "phase_trace_sha256": sha256_file(trace_path),
        },
    )
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
    deterministic_values: list[list[float]] | None = None,
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
    if policy.get("deterministic_zero"):
        exact_sets = deterministic_values or calibration_sets
        all_values = [value for values in exact_sets for value in values]
        nonzero_values = [value for value in all_values if value != 0.0]
        if not nonzero_values:
            result.update(
                {"rule": "exact_zero", "absolute_margin": 0.0, "relative_margin": None}
            )
            return result
        result.update(
            {
                "rule": "exact_zero_violation",
                "absolute_margin": None,
                "relative_margin": None,
                "status": "CALIBRATION_REVIEW_REQUIRED",
                "reason": "deterministic-zero-observation-was-nonzero",
                "nonzero_run_count": len(nonzero_values),
                "nonzero_min": min(nonzero_values),
                "nonzero_max": max(nonzero_values),
            }
        )
        return result
    if min(set_medians) <= 0.0 or center < resolution:
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
    deterministic_zero_violations: list[str] = []
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
        for metric in METRIC_POLICIES:
            policy = policy_for_scenario(scenario, metric)
            metric_proposal: dict[str, Any] = {}
            deterministic_values: list[list[float]] | None = None
            if policy.get("deterministic_zero"):
                deterministic_values = []
                for calibration_set in loaded_sets:
                    raw_totals = [
                        reducer_value(run["observations"][metric], "total")
                        for run in calibration_set
                    ]
                    retained_totals = [
                        value for value in raw_totals if value is not None
                    ]
                    if len(retained_totals) != args.runs_per_set:
                        raise RuntimeError(
                            f"{scenario_id}/{metric}/total has incomplete exact-zero values"
                        )
                    deterministic_values.append(retained_totals)
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
                    deterministic_values,
                )
                if rule["status"] == "CALIBRATION_REVIEW_REQUIRED":
                    review_required.append(f"{scenario_id}/{metric}/{reducer}")
                if rule["rule"] == "exact_zero_violation":
                    deterministic_zero_violations.append(
                        f"{scenario_id}/{metric}/{reducer}"
                    )
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
    proposal["deterministic_zero_violations"] = deterministic_zero_violations
    if deterministic_zero_violations:
        proposal["status"] = "CALIBRATION_FAILED"
    return proposal


def validate_fixed_protocol(args: argparse.Namespace) -> None:
    fixed_protocol = {
        "graphics_backend": (args.graphics_backend, "native"),
        "width": (args.width, FIXED_WIDTH),
        "height": (args.height, FIXED_HEIGHT),
        "warmup_runs": (args.warmup_runs, FIXED_WARMUP_RUNS),
        "warmup_frames": (args.warmup_frames, FIXED_WARMUP_FRAMES),
        "frames": (args.frames, FIXED_MEASURED_FRAMES),
        "sets": (args.sets, FIXED_SETS),
        "runs_per_set": (args.runs_per_set, FIXED_RUNS_PER_SET),
        "unstable_drift_threshold": (
            args.unstable_drift_threshold,
            FIXED_UNSTABLE_DRIFT_THRESHOLD,
        ),
    }
    deviations = {
        name: {"actual": actual, "required": required}
        for name, (actual, required) in fixed_protocol.items()
        if actual != required
    }
    if deviations:
        raise ValueError(f"noncanonical retained calibration protocol: {deviations}")


def main() -> int:
    args = parse_args()
    validate_fixed_protocol(args)
    if not args.manifest_only and not args.executable.is_file():
        raise FileNotFoundError(args.executable)

    manifest = scenario_manifest(args)
    manifest = write_immutable_json(args.output_dir / "scenario_manifest.json", manifest)
    if args.manifest_only:
        return 0

    args.environment_fingerprint = run_environment_probe(args)

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
    if proposal["status"] == "CALIBRATION_FAILED":
        raise RuntimeError(
            "deterministic-zero calibration invariants failed; retained proposal records violations"
        )
    print(args.output_dir.resolve())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - CLI must retain/report failed phases.
        print(f"calibration failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
