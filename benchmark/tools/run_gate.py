#!/usr/bin/env python3
"""Run or retain an append-only vnm_plot checkpoint gate evidence bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import uuid
from collections.abc import Mapping
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from phase_trace import parse_and_validate_phase_trace


RENDERER_ENVIRONMENT_FIELDS = ("GALLIUM_DRIVER", "LP_NUM_THREADS")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def attempt_identity() -> str:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    return f"{timestamp}-{uuid.uuid4().hex[:8]}"


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


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


def run_capture(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def command_output(command: list[str], cwd: Path) -> str:
    try:
        completed = run_capture(command, cwd)
    except OSError as exc:
        return f"unavailable: {exc}"
    output = completed.stdout.strip() or completed.stderr.strip()
    if completed.returncode != 0:
        return f"unavailable (exit {completed.returncode}): {output}"
    return output


def actionlint_candidates(args: argparse.Namespace) -> list[Path]:
    executable_name = "actionlint.exe" if os.name == "nt" else "actionlint"
    candidates: list[Path] = []
    if args.actionlint:
        candidates.append(args.actionlint)
    discovered = shutil.which("actionlint")
    if discovered:
        candidates.append(Path(discovered))
    candidates.append(
        args.build_dir / "tools" / "actionlint-1.7.12" / executable_name
    )
    return candidates


def actionlint_version_token(output: str) -> str:
    lines = output.splitlines()
    return lines[0].strip() if lines else ""


def resolve_actionlint(args: argparse.Namespace) -> tuple[Path, dict[str, str]]:
    for candidate in actionlint_candidates(args):
        if not candidate.is_file():
            continue
        resolved = candidate.resolve()
        version = command_output([str(resolved), "-version"], args.source_root)
        if actionlint_version_token(version) != "1.7.12":
            raise RuntimeError(
                f"actionlint must be pinned to 1.7.12, got {version!r} from {resolved}"
            )
        return resolved, {
            "path": str(resolved),
            "version": version,
            "sha256": sha256_file(resolved),
        }
    raise RuntimeError(
        "actionlint 1.7.12 bootstrap is missing; supply --actionlint or place the "
        "verified binary below <build>/tools/actionlint-1.7.12"
    )


def parse_environment_block(output: str) -> dict[str, str]:
    environment: dict[str, str] = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        name, value = line.split("=", 1)
        if name:
            environment[name] = value
    return environment


def find_vcvars64(source_root: Path) -> Path:
    install_root = os.environ.get("VSINSTALLDIR")
    if install_root:
        candidate = Path(install_root) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
        if candidate.is_file():
            return candidate.resolve()

    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = (
        Path(program_files_x86)
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if vswhere.is_file():
        completed = run_capture(
            [
                str(vswhere),
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property",
                "installationPath",
            ],
            source_root,
        )
        if completed.returncode == 0 and completed.stdout.strip():
            candidate = (
                Path(completed.stdout.strip())
                / "VC"
                / "Auxiliary"
                / "Build"
                / "vcvars64.bat"
            )
            if candidate.is_file():
                return candidate.resolve()
    raise RuntimeError("Visual Studio x64 environment bootstrap is unavailable")


def msvc_environment_is_x64(environment: Mapping[str, str]) -> bool:
    return (
        all(environment.get(name) for name in ("INCLUDE", "LIB", "VCToolsInstallDir"))
        and environment.get("VSCMD_ARG_TGT_ARCH", "").lower() in {"x64", "amd64"}
    )


def initialize_msvc_environment(source_root: Path) -> dict[str, str]:
    if os.name != "nt":
        return {"status": "not-applicable"}
    if msvc_environment_is_x64(os.environ):
        return {
            "status": "already-initialized",
            "target_architecture": os.environ["VSCMD_ARG_TGT_ARCH"],
            "vctools_version": os.environ.get("VCToolsVersion", "unavailable"),
            "windows_sdk_version": os.environ.get("WindowsSDKVersion", "unavailable"),
        }

    vcvars = find_vcvars64(source_root)
    completed = subprocess.run(
        f'call "{vcvars}" >nul && set',
        cwd=source_root,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        shell=True,
        executable=os.environ.get("COMSPEC", "cmd.exe"),
    )
    if completed.returncode != 0:
        raise RuntimeError(
            completed.stderr.strip() or "Visual Studio x64 environment bootstrap failed"
        )
    initialized = parse_environment_block(completed.stdout)
    if not msvc_environment_is_x64(initialized):
        raise RuntimeError("Visual Studio bootstrap did not return an x64 environment")
    os.environ.update(initialized)
    return {
        "status": "initialized",
        "target_architecture": initialized["VSCMD_ARG_TGT_ARCH"],
        "vcvars64": str(vcvars),
        "vctools_version": initialized.get("VCToolsVersion", "unavailable"),
        "windows_sdk_version": initialized.get("WindowsSDKVersion", "unavailable"),
    }


def git_output(source_root: Path, *arguments: str) -> str:
    completed = run_capture(["git", *arguments], source_root)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "git command failed")
    return completed.stdout.rstrip("\r\n")


def source_identity(source_root: Path) -> dict[str, Any]:
    commit = git_output(source_root, "rev-parse", "HEAD")
    status = git_output(source_root, "status", "--porcelain=v1", "--untracked-files=all")
    diff_completed = run_capture(["git", "diff", "--binary", "HEAD", "--"], source_root)
    if diff_completed.returncode != 0:
        raise RuntimeError(diff_completed.stderr.strip() or "git diff failed")
    diff = diff_completed.stdout
    paths = git_output(
        source_root,
        "ls-files",
        "--cached",
        "--others",
        "--exclude-standard",
        "-z",
    ).split("\0")
    digest = hashlib.sha256()
    retained_paths = []
    for relative in (path for path in paths if path):
        path = source_root / relative
        if not path.is_file():
            continue
        encoded = relative.replace("\\", "/").encode("utf-8", errors="surrogateescape")
        digest.update(encoded)
        digest.update(b"\0")
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
        retained_paths.append(relative.replace("\\", "/"))
    exact_sha256 = digest.hexdigest()
    return {
        "commit": commit,
        "git_tree": git_output(source_root, "rev-parse", "HEAD^{tree}"),
        "dirty": bool(status),
        "status": status,
        "diff": diff,
        "diff_sha256": hashlib.sha256(diff.encode("utf-8")).hexdigest(),
        "exact_tree_sha256": exact_sha256,
        "retained_path_count": len(retained_paths),
        "slug": f"{commit[:12]}-{exact_sha256[:12]}-{'dirty' if status else 'clean'}",
    }


def unavailable_source_identity(error: Exception) -> dict[str, Any]:
    reason = f"{type(error).__name__}: {error}"
    return {
        "commit": "unavailable",
        "git_tree": "unavailable",
        "dirty": True,
        "status": f"unavailable: {reason}",
        "diff": "",
        "diff_sha256": hashlib.sha256(b"").hexdigest(),
        "exact_tree_sha256": "unavailable",
        "retained_path_count": 0,
        "slug": "unavailable-source",
    }


def parse_cache(build_dir: Path) -> dict[str, str]:
    cache_path = build_dir / "CMakeCache.txt"
    if not cache_path.is_file():
        return {}
    result: dict[str, str] = {}
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line or ":" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key, _ = key_and_type.split(":", 1)
        result[key] = value
    return result


def repository_identity(path: Path) -> dict[str, Any]:
    if not path.is_dir():
        return {"path": str(path), "status": "ENVIRONMENT_BOOTSTRAP_REQUIRED"}
    result: dict[str, Any] = {"path": str(path.resolve())}
    try:
        status = git_output(
            path,
            "status",
            "--porcelain=v1",
            "--untracked-files=all",
        )
        result.update(
            {
                "commit": git_output(path, "rev-parse", "HEAD"),
                "dirty": bool(status),
                "status": status,
            }
        )
    except (OSError, RuntimeError) as exc:
        result["status"] = f"unavailable: {exc}"
    return result


def preflight(
    args: argparse.Namespace,
    source: dict[str, Any],
    attempt_dir: Path,
) -> dict[str, Any]:
    cache = parse_cache(args.build_dir)
    standards_pipeline = args.standards_root / "tools" / "style_pipeline.py"
    dependency_value = cache.get("vnm_msdf_text_SOURCE_DIR", "")
    dependency_root = (
        Path(dependency_value)
        if dependency_value
        else args.source_root.parent / "vnm_msdf_text"
    )
    relevant_environment = {
        name: os.environ.get(name, "")
        for name in (
            "CI",
            "CMAKE_BUILD_TYPE",
            "GITHUB_ACTIONS",
            "GITHUB_JOB",
            "GITHUB_REF",
            "GITHUB_REPOSITORY",
            "GITHUB_RUN_ATTEMPT",
            "GITHUB_RUN_ID",
            "GALLIUM_DRIVER",
            "LP_NUM_THREADS",
            "QT_QPA_PLATFORM",
            "QSG_RHI_BACKEND",
            "VULKAN_SDK",
        )
    }
    payload: dict[str, Any] = {
        "recorded_at_utc": utc_now(),
        "source": {key: value for key, value in source.items() if key != "diff"},
        "source_diff_path": "preflight/source.diff",
        "platform": platform.platform(),
        "architecture": platform.machine(),
        "python": {"version": sys.version, "executable": sys.executable},
        "cmake": command_output(["cmake", "--version"], args.source_root),
        "ctest": command_output(["ctest", "--version"], args.source_root),
        "compiler": cache.get("CMAKE_CXX_COMPILER", "unavailable-before-configure"),
        "compiler_id": cache.get("CMAKE_CXX_COMPILER_ID", "unavailable-before-configure"),
        "qt_directory": cache.get("Qt6_DIR", "unavailable-before-configure"),
        "standards": {
            **repository_identity(args.standards_root),
            "pipeline": str(standards_pipeline),
            "pipeline_sha256": (
                sha256_file(standards_pipeline) if standards_pipeline.is_file() else "unavailable"
            ),
        },
        "dependency": repository_identity(dependency_root),
        "actionlint": {
            "status": "ENVIRONMENT_BOOTSTRAP_REQUIRED",
        },
        "environment": relevant_environment,
        "invocation": list(sys.argv),
        "ci": {
            "run_url": (
                f"https://github.com/{os.environ.get('GITHUB_REPOSITORY', '')}/actions/runs/"
                f"{os.environ.get('GITHUB_RUN_ID', '')}"
                if os.environ.get("GITHUB_RUN_ID")
                else ""
            ),
            "job": os.environ.get("GITHUB_JOB", ""),
        },
    }
    preflight_dir = attempt_dir / "preflight"
    preflight_dir.mkdir(parents=True, exist_ok=False)
    (preflight_dir / "source.diff").write_bytes(source["diff"].encode("utf-8"))
    write_json(preflight_dir / "preflight.json", payload)
    return payload


class Gate:
    def __init__(
        self,
        args: argparse.Namespace,
        attempt_dir: Path,
        source: dict[str, Any],
        preflight_payload: dict[str, Any],
    ) -> None:
        self.args = args
        self.attempt_dir = attempt_dir
        self.started_at = utc_now()
        self.phases: list[dict[str, Any]] = []
        self.manifest: dict[str, Any] = {
            "schema_version": 1,
            "batch": args.batch,
            "checkpoint": args.checkpoint,
            "mode": args.mode,
            "attempt": attempt_dir.name,
            "recovery_of": args.recovery_of,
            "started_at_utc": self.started_at,
            "ended_at_utc": None,
            "status": "RUNNING",
            "current_phase": "preflight",
            "command": list(sys.argv),
            "exit_status": None,
            "inputs": {
                "source": {key: value for key, value in source.items() if key != "diff"},
                "build_dir": str(args.build_dir.resolve()),
                "configuration": args.config,
                "preflight": preflight_payload,
            },
            "phases": self.phases,
            "artifact_hashes": {},
            "status_history": [
                {"status": "RUNNING", "recorded_at_utc": self.started_at}
            ],
        }
        self.write_manifest()

    def write_manifest(self) -> None:
        write_json(self.attempt_dir / "gate_manifest.json", self.manifest)

    def run_phase(
        self,
        name: str,
        command: list[str],
        *,
        cwd: Path | None = None,
        timeout: float | None = None,
    ) -> None:
        index = len(self.phases) + 1
        phase_dir = self.attempt_dir / "phases" / f"{index:02d}-{name}"
        phase_dir.mkdir(parents=True, exist_ok=False)
        phase = {
            "name": name,
            "started_at_utc": utc_now(),
            "ended_at_utc": None,
            "command": command,
            "working_directory": str((cwd or self.args.source_root).resolve()),
            "returncode": None,
            "status": "RUNNING",
        }
        self.phases.append(phase)
        self.manifest["current_phase"] = name
        self.write_manifest()
        try:
            completed = subprocess.run(
                command,
                cwd=cwd or self.args.source_root,
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
            )
            stdout = completed.stdout
            stderr = completed.stderr
            phase["returncode"] = completed.returncode
            phase["status"] = "PASS" if completed.returncode == 0 else "FAIL"
        except subprocess.TimeoutExpired as exc:
            stdout = captured_text(exc.stdout)
            stderr = captured_text(exc.stderr)
            phase["status"] = "TIMEOUT"
            phase["timeout_seconds"] = timeout
        except OSError as exc:
            stdout = ""
            stderr = str(exc)
            phase["status"] = "ENVIRONMENT_BOOTSTRAP_REQUIRED"
        (phase_dir / "stdout.txt").write_text(stdout, encoding="utf-8")
        (phase_dir / "stderr.txt").write_text(stderr, encoding="utf-8")
        phase["ended_at_utc"] = utc_now()
        write_json(phase_dir / "phase.json", phase)
        self.write_manifest()
        if phase["status"] != "PASS":
            raise RuntimeError(f"gate phase {name} ended with {phase['status']}")

    def retain_smoke(self) -> None:
        source = self.args.build_dir / "benchmark" / "smoke-reports"
        destination = self.attempt_dir / "smoke-evidence"
        backend_root = source / "native"
        if backend_root.is_dir():
            candidates = sorted(path for path in backend_root.iterdir() if path.is_dir())
            if candidates:
                selected = candidates[-1]
                shutil.copytree(selected, destination / "native" / selected.name)
        validations = []
        graphics = []
        if destination.is_dir():
            for path in sorted(destination.rglob("smoke_validation.json")):
                validation = json.loads(path.read_text(encoding="utf-8"))
                validations.append(validation)
                if validation.get("status") != "PASS":
                    continue
                artifacts = list(path.parent.glob("inspector_benchmark_*.json"))
                if len(artifacts) != 1:
                    validation["gate_error"] = "missing-or-ambiguous-raw-artifact"
                    continue
                invocations = list(path.parent.glob("smoke_invocation.json"))
                if len(invocations) != 1:
                    validation["gate_error"] = "missing-or-ambiguous-smoke-invocation"
                    continue
                invocation = json.loads(invocations[0].read_text(encoding="utf-8"))
                if validation.get("artifact") != artifacts[0].name or validation.get(
                    "artifact_sha256"
                ) != sha256_file(artifacts[0]):
                    validation["gate_error"] = "raw-artifact-hash-mismatch"
                    continue
                metadata = json.loads(
                    artifacts[0].read_text(encoding="utf-8")
                )["metadata"]
                traces = list(path.parent.glob("benchmark_phase_trace_*.jsonl"))
                if len(traces) != 1:
                    validation["gate_error"] = "missing-or-ambiguous-phase-trace"
                    continue
                trace = traces[0]
                if validation.get("phase_trace") != trace.name or validation.get(
                    "phase_trace_sha256"
                ) != sha256_file(trace):
                    validation["gate_error"] = "phase-trace-hash-mismatch"
                    continue
                try:
                    parse_and_validate_phase_trace(
                        trace.read_text(encoding="utf-8"),
                        int(metadata["warmup_frames"]),
                        int(metadata["measured_frames"]),
                    )
                except (KeyError, TypeError, ValueError, RuntimeError) as exc:
                    validation["gate_error"] = "incomplete-phase-trace"
                    validation["phase_trace_error"] = str(exc)
                    continue
                expected_source = self.manifest["inputs"]["source"]
                preflight = self.manifest["inputs"]["preflight"]
                dependency = preflight.get("dependency", {})
                expected = {
                    "build_source_dirty": "false" if not expected_source["dirty"] else "true",
                    "build_source_commit": expected_source["commit"],
                    "build_source_diff_sha256": expected_source["diff_sha256"],
                    "build_source_tree": expected_source["git_tree"],
                    "source_commit": expected_source["commit"],
                    "source_diff_sha256": expected_source["diff_sha256"],
                    "source_dirty": "true" if expected_source["dirty"] else "false",
                    "source_tree": expected_source["exact_tree_sha256"],
                }
                if dependency.get("commit"):
                    expected.update(
                        {
                            "build_dependency_commit": dependency["commit"],
                            "build_dependency_dirty": (
                                "true" if dependency.get("dirty") else "false"
                            ),
                            "dependency_commit": dependency["commit"],
                            "dependency_dirty": (
                                "true" if dependency.get("dirty") else "false"
                            ),
                        }
                    )
                environment = preflight.get("environment", {})
                mismatches = {
                    field: {"expected": value, "actual": metadata.get(field)}
                    for field, value in expected.items()
                    if metadata.get(field) != value
                }
                mismatches.update(
                    renderer_environment_mismatches(
                        environment,
                        invocation.get("renderer_environment", {}),
                        metadata,
                    )
                )
                if mismatches:
                    validation["gate_error"] = "execution-fingerprint-mismatch"
                    validation["fingerprint_mismatches"] = mismatches
                graphics.append(
                    {
                        field: metadata.get(field, "")
                        for field in (
                            "actual_graphics_backend",
                            "device_id",
                            "device_name",
                            "driver_identity",
                            "driver_version",
                            "vendor_id",
                        )
                    }
                )
        self.manifest["inputs"]["preflight"]["graphics"] = graphics
        write_json(
            self.attempt_dir / "preflight" / "preflight.json",
            self.manifest["inputs"]["preflight"],
        )
        status = "PASS" if len(validations) == 1 and all(
            item.get("status") == "PASS" and not item.get("gate_error")
            for item in validations
        ) else "FAIL"
        phase = {
            "name": "retain-smoke-evidence",
            "started_at_utc": utc_now(),
            "ended_at_utc": utc_now(),
            "command": [],
            "returncode": 0 if status == "PASS" else 1,
            "status": status,
            "validation_count": len(validations),
            "validations": validations,
        }
        self.phases.append(phase)
        self.manifest["current_phase"] = phase["name"]
        self.write_manifest()
        if status != "PASS":
            raise RuntimeError("no complete passing smoke evidence was retained")

    def finalize(self, status: str, exit_status: int, reason: str = "") -> None:
        if status == "PASS":
            raise RuntimeError(
                "checkpoint PASS may only be recorded by approve_gate.py after "
                "owner review of the retained calibration proposal"
            )
        self.manifest["status"] = status
        self.manifest["exit_status"] = exit_status
        self.manifest["ended_at_utc"] = utc_now()
        self.manifest["current_phase"] = self.phases[-1]["name"] if self.phases else "preflight"
        if reason:
            self.manifest["reason"] = reason
        self.manifest["status_history"].append(
            {"status": status, "recorded_at_utc": self.manifest["ended_at_utc"]}
        )
        hashes: dict[str, str] = {}
        for path in sorted(self.attempt_dir.rglob("*")):
            if path.is_file() and path.name != "gate_manifest.json":
                relative = path.relative_to(self.attempt_dir).as_posix()
                hashes[relative] = sha256_file(path)
        self.manifest["artifact_hashes"] = hashes
        self.write_manifest()

    def refresh_configured_preflight(self) -> None:
        cache = parse_cache(self.args.build_dir)
        preflight_payload = self.manifest["inputs"]["preflight"]
        preflight_payload["compiler"] = cache.get("CMAKE_CXX_COMPILER", "unavailable")
        preflight_payload["compiler_id"] = cache.get("CMAKE_CXX_COMPILER_ID", "unavailable")
        preflight_payload["cmake_generator"] = cache.get("CMAKE_GENERATOR", "unavailable")
        preflight_payload["cmake_generator_platform"] = cache.get(
            "CMAKE_GENERATOR_PLATFORM",
            "",
        )
        preflight_payload["qt_directory"] = cache.get("Qt6_DIR", "unavailable")
        dependency_value = cache.get("vnm_msdf_text_SOURCE_DIR", "")
        if dependency_value:
            preflight_payload["dependency"] = repository_identity(Path(dependency_value))
        write_json(
            self.attempt_dir / "preflight" / "preflight.json",
            preflight_payload,
        )
        self.write_manifest()


def retain_gate_failure(gate: Gate, error: Exception) -> None:
    failure = f"{type(error).__name__}: {error}\n"
    (gate.attempt_dir / "failure.txt").write_text(failure, encoding="utf-8")
    gate.finalize("FAIL", 1, str(error))


def renderer_environment_fingerprint(environment: dict[str, Any]) -> dict[str, str]:
    return {
        f"env.{name}": str(environment.get(name, "") or "unset")
        for name in RENDERER_ENVIRONMENT_FIELDS
    }


def renderer_environment_mismatches(
    preflight: dict[str, Any],
    invocation: dict[str, Any],
    metadata: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    expected = renderer_environment_fingerprint(preflight)
    invoked = renderer_environment_fingerprint(invocation)
    mismatches: dict[str, dict[str, Any]] = {}
    for field, value in expected.items():
        if invoked[field] != value:
            mismatches[f"invocation.{field}"] = {
                "expected": value,
                "actual": invoked[field],
            }
        if metadata.get(field) != value:
            mismatches[f"raw.{field}"] = {
                "expected": value,
                "actual": metadata.get(field),
            }
    return mismatches


def parse_args() -> argparse.Namespace:
    default_source = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=default_source)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument(
        "--standards-root",
        type=Path,
        default=Path(r"C:\plms\varinomics\varinomics-standards"),
    )
    parser.add_argument("--batch", default="batch-2")
    parser.add_argument("--checkpoint", default="2.1")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--mode", choices=("full", "ci-retain"), default="full")
    parser.add_argument("--recovery-of")
    parser.add_argument("--upstream-status", default="success")
    parser.add_argument("--actionlint", type=Path)
    return parser.parse_args()


def configure_command(args: argparse.Namespace) -> list[str]:
    return [
        "cmake",
        "-S",
        str(args.source_root.resolve()),
        "-B",
        str(args.build_dir.resolve()),
        "-DVNM_PLOT_ENABLE_TEXT=ON",
        "-DVNM_PLOT_BUILD_TESTS=ON",
        "-DVNM_PLOT_BUILD_BENCHMARK=ON",
        "-DVNM_PLOT_BUILD_BENCHMARK_TESTS=ON",
        f"-DCMAKE_BUILD_TYPE={args.config}",
    ]


def resolve_benchmark_executable(build_dir: Path, config: str) -> Path:
    executable_name = "vnm_plot_benchmark.exe" if os.name == "nt" else "vnm_plot_benchmark"
    candidates = (
        build_dir / "benchmark" / config / executable_name,
        build_dir / "benchmark" / executable_name,
    )
    retained = [candidate.resolve() for candidate in candidates if candidate.is_file()]
    if len(retained) != 1:
        raise RuntimeError(
            "expected exactly one benchmark executable for configuration "
            f"{config}, found {[str(path) for path in retained]}"
        )
    return retained[0]


def main() -> int:
    args = parse_args()
    args.source_root = args.source_root.resolve()
    args.build_dir = args.build_dir.resolve()
    args.standards_root = args.standards_root.resolve()
    args.build_dir.mkdir(parents=True, exist_ok=True)
    attempt = attempt_identity()
    try:
        source = source_identity(args.source_root)
    except Exception as exc:  # noqa: BLE001 - retain bootstrap failure evidence.
        source = unavailable_source_identity(exc)
        attempt_dir = (
            args.build_dir
            / "gate-artifacts"
            / args.batch
            / source["slug"]
            / attempt
        )
        attempt_dir.mkdir(parents=True, exist_ok=False)
        bootstrap = {
            "status": "SOURCE_IDENTITY_FAILED",
            "recorded_at_utc": utc_now(),
            "error": f"{type(exc).__name__}: {exc}",
        }
        gate = Gate(args, attempt_dir, source, bootstrap)
        retain_gate_failure(gate, exc)
        print(f"gate failed: {exc}; retained {attempt_dir}", file=sys.stderr)
        return 1
    attempt_dir = (
        args.build_dir
        / "gate-artifacts"
        / args.batch
        / source["slug"]
        / attempt
    )
    attempt_dir.mkdir(parents=True, exist_ok=False)
    gate = Gate(
        args,
        attempt_dir,
        source,
        {"status": "COLLECTING", "recorded_at_utc": utc_now()},
    )
    try:
        preflight_payload = preflight(args, source, attempt_dir)
        gate.manifest["inputs"]["preflight"] = preflight_payload
        gate.write_manifest()
        if args.mode == "ci-retain":
            gate.retain_smoke()
            if args.upstream_status.lower() != "success":
                raise RuntimeError(f"upstream CI status was {args.upstream_status}")
            gate.finalize("DIAGNOSTIC_PASS", 0)
            print(attempt_dir)
            return 0
        else:
            if args.checkpoint != "2.1":
                raise RuntimeError("the full retained gate currently implements checkpoint 2.1")
            if source["dirty"]:
                raise RuntimeError("full checkpoint gate requires a clean source tree")
            actionlint, actionlint_metadata = resolve_actionlint(args)
            bootstrap_metadata = initialize_msvc_environment(args.source_root)
            gate.manifest["inputs"]["preflight"]["actionlint"] = actionlint_metadata
            gate.manifest["inputs"]["preflight"]["msvc_environment"] = bootstrap_metadata
            write_json(
                gate.attempt_dir / "preflight" / "preflight.json",
                gate.manifest["inputs"]["preflight"],
            )
            gate.write_manifest()
            pipeline = args.standards_root / "tools" / "style_pipeline.py"
            if not pipeline.is_file():
                raise RuntimeError(f"style pipeline bootstrap is missing: {pipeline}")
            gate.run_phase(
                "style",
                [sys.executable, str(pipeline), "--root", str(args.source_root)],
            )
            gate.run_phase("actionlint", [str(actionlint)])
            gate.run_phase("configure", configure_command(args), timeout=300)
            gate.refresh_configured_preflight()
            dependency = gate.manifest["inputs"]["preflight"]["dependency"]
            if dependency.get("dirty", True):
                raise RuntimeError("full checkpoint gate requires a clean dependency tree")
            gate.run_phase(
                "build",
                ["cmake", "--build", str(args.build_dir), "--config", args.config],
                timeout=1200,
            )
            gate.run_phase(
                "ctest",
                [
                    "ctest",
                    "--test-dir",
                    str(args.build_dir),
                    "-C",
                    args.config,
                    "--output-on-failure",
                ],
                timeout=1200,
            )
            gate.retain_smoke()
            executable = resolve_benchmark_executable(args.build_dir, args.config)
            calibration_dir = attempt_dir / "calibration"
            gate.run_phase(
                "calibration",
                [
                    sys.executable,
                    str(args.source_root / "benchmark" / "tools" / "calibrate.py"),
                    "--executable",
                    str(executable),
                    "--output-dir",
                    str(calibration_dir),
                ],
                timeout=7200,
            )
            proposal = calibration_dir / "proposed_noise_margins.json"
            proposal_sha256 = sha256_file(proposal)
            gate.manifest["inputs"]["calibration_proposal"] = {
                "path": proposal.relative_to(attempt_dir).as_posix(),
                "sha256": proposal_sha256,
            }
        gate.finalize(
            "CALIBRATION_REVIEW_REQUIRED",
            2,
            "owner approval must reference the retained calibration proposal SHA256",
        )
        print(attempt_dir)
        print(f"proposal_sha256={proposal_sha256}")
        return 2
    except Exception as exc:  # noqa: BLE001 - retain the exact failed phase.
        retain_gate_failure(gate, exc)
        print(f"gate failed: {exc}; retained {attempt_dir}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
