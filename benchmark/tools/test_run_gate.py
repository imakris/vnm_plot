#!/usr/bin/env python3
"""Regression tests for append-only checkpoint gate evidence."""

from __future__ import annotations

import argparse
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


MODULE_PATH = Path(__file__).with_name("run_gate.py")
SPEC = importlib.util.spec_from_file_location("vnm_plot_run_gate", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {MODULE_PATH}")
run_gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(run_gate)


class GateEvidenceTests(unittest.TestCase):
    @staticmethod
    def main_args(root: Path) -> argparse.Namespace:
        return argparse.Namespace(
            source_root=root / "source",
            build_dir=root / "build",
            standards_root=root / "standards",
            batch="batch-2",
            checkpoint="2.1",
            config="Release",
            mode="ci-retain",
            recovery_of=None,
            upstream_status="success",
            actionlint=None,
        )

    def test_actionlint_version_pin_is_exact(self) -> None:
        self.assertEqual(
            run_gate.actionlint_version_token("1.7.12\nbuild metadata\n"),
            "1.7.12",
        )
        self.assertNotEqual(
            run_gate.actionlint_version_token("1.7.120\nbuild metadata\n"),
            "1.7.12",
        )

    def test_timeout_output_is_retained_as_text(self) -> None:
        self.assertEqual(run_gate.captured_text(b"partial\xff"), "partial�")

    def test_environment_block_parser_preserves_values_containing_equals(self) -> None:
        self.assertEqual(
            run_gate.parse_environment_block("A=one=two\r\nB=three\r\n"),
            {"A": "one=two", "B": "three"},
        )

    def test_renderer_environment_fingerprint_preserves_unset_and_values(self) -> None:
        self.assertEqual(
            run_gate.renderer_environment_fingerprint(
                {"GALLIUM_DRIVER": "softpipe", "LP_NUM_THREADS": ""}
            ),
            {
                "env.GALLIUM_DRIVER": "softpipe",
                "env.LP_NUM_THREADS": "unset",
            },
        )

    def test_renderer_environment_three_way_match_is_clean(self) -> None:
        preflight = {"GALLIUM_DRIVER": "softpipe", "LP_NUM_THREADS": "1"}
        metadata = {
            "env.GALLIUM_DRIVER": "softpipe",
            "env.LP_NUM_THREADS": "1",
        }
        self.assertEqual(
            run_gate.renderer_environment_mismatches(
                preflight,
                dict(preflight),
                metadata,
            ),
            {},
        )

    def test_renderer_environment_three_way_tamper_is_rejected(self) -> None:
        mismatches = run_gate.renderer_environment_mismatches(
            {"GALLIUM_DRIVER": "softpipe", "LP_NUM_THREADS": "1"},
            {"GALLIUM_DRIVER": "llvmpipe", "LP_NUM_THREADS": "1"},
            {
                "env.GALLIUM_DRIVER": "softpipe",
                "env.LP_NUM_THREADS": "8",
            },
        )
        self.assertEqual(
            set(mismatches),
            {"invocation.env.GALLIUM_DRIVER", "raw.env.LP_NUM_THREADS"},
        )

    def test_msvc_initialized_environment_must_target_x64(self) -> None:
        common = {
            "INCLUDE": "include",
            "LIB": "lib",
            "VCToolsInstallDir": "tools",
        }
        self.assertFalse(run_gate.msvc_environment_is_x64(common))
        self.assertFalse(
            run_gate.msvc_environment_is_x64(
                {**common, "VSCMD_ARG_TGT_ARCH": "x86"}
            )
        )
        self.assertTrue(
            run_gate.msvc_environment_is_x64(
                {**common, "VSCMD_ARG_TGT_ARCH": "x64"}
            )
        )

    def test_attempt_identity_is_unique(self) -> None:
        first = run_gate.attempt_identity()
        second = run_gate.attempt_identity()
        self.assertNotEqual(first, second)
        self.assertRegex(first, r"^\d{8}T\d{6}\.\d{6}Z-[0-9a-f]{8}$")

    def test_freebsd_capacity_evidence_is_retained(self) -> None:
        with (
            mock.patch.object(run_gate.platform, "system", return_value="FreeBSD"),
            mock.patch.object(
                run_gate,
                "command_output",
                side_effect=("10737418240", "9663676416", "swap", "limits"),
            ) as command_output,
        ):
            evidence = run_gate.guest_capacity_evidence(Path("."))
        self.assertEqual(evidence["physical_memory_bytes"], "10737418240")
        self.assertEqual(evidence["user_memory_bytes"], "9663676416")
        self.assertEqual(evidence["swap"], "swap")
        self.assertEqual(evidence["process_limits"], "limits")
        self.assertEqual(command_output.call_count, 4)

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
            gate.finalize("DIAGNOSTIC_PASS", 0)
            manifest = json.loads(
                (attempt / "gate_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["status"], "DIAGNOSTIC_PASS")
            self.assertIn("evidence.txt", manifest["artifact_hashes"])
            self.assertNotIn("gate_manifest.json", manifest["artifact_hashes"])

    def test_finalize_rejects_checkpoint_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            attempt = root / "attempt"
            attempt.mkdir()
            args = argparse.Namespace(
                batch="batch-2",
                checkpoint="2.1",
                mode="full",
                recovery_of=None,
                build_dir=root / "build",
                config="Release",
                source_root=root,
            )
            source = {
                "commit": "a" * 40,
                "slug": "source",
                "dirty": False,
                "diff": "",
            }
            gate = run_gate.Gate(args, attempt, source, {"status": "test"})
            with self.assertRaisesRegex(RuntimeError, "not recorded"):
                gate.finalize("PASS", 0)

    def test_multi_config_executable_is_resolved(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            executable_name = (
                "vnm_plot_benchmark.exe"
                if run_gate.os.name == "nt"
                else "vnm_plot_benchmark"
            )
            executable = build / "benchmark" / "Release" / executable_name
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"benchmark")
            self.assertEqual(
                run_gate.resolve_benchmark_executable(build, "Release"),
                executable.resolve(),
            )

    def test_ambiguous_executable_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            executable_name = (
                "vnm_plot_benchmark.exe"
                if run_gate.os.name == "nt"
                else "vnm_plot_benchmark"
            )
            for executable in (
                build / "benchmark" / executable_name,
                build / "benchmark" / "Release" / executable_name,
            ):
                executable.parent.mkdir(parents=True, exist_ok=True)
                executable.write_bytes(b"benchmark")
            with self.assertRaises(RuntimeError):
                run_gate.resolve_benchmark_executable(build, "Release")

    def test_source_identity_failure_retains_fail_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = self.main_args(root)
            args.source_root.mkdir()
            with (
                mock.patch.object(run_gate, "parse_args", return_value=args),
                mock.patch.object(
                    run_gate,
                    "source_identity",
                    side_effect=RuntimeError("source unavailable"),
                ),
            ):
                self.assertEqual(run_gate.main(), 1)
            manifests = list(args.build_dir.rglob("gate_manifest.json"))
            self.assertEqual(len(manifests), 1)
            manifest = json.loads(manifests[0].read_text(encoding="utf-8"))
            self.assertEqual(manifest["status"], "FAIL")
            self.assertEqual(
                manifest["inputs"]["preflight"]["status"],
                "SOURCE_IDENTITY_FAILED",
            )
            self.assertIn("source unavailable", manifest["reason"])
            self.assertIn("failure.txt", manifest["artifact_hashes"])

    def test_preflight_failure_retains_fail_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = self.main_args(root)
            args.source_root.mkdir()
            source = {
                "commit": "a" * 40,
                "slug": "source-clean",
                "dirty": False,
                "diff": "",
            }
            with (
                mock.patch.object(run_gate, "parse_args", return_value=args),
                mock.patch.object(run_gate, "source_identity", return_value=source),
                mock.patch.object(
                    run_gate,
                    "preflight",
                    side_effect=RuntimeError("preflight unavailable"),
                ),
            ):
                self.assertEqual(run_gate.main(), 1)
            manifests = list(args.build_dir.rglob("gate_manifest.json"))
            self.assertEqual(len(manifests), 1)
            manifest = json.loads(manifests[0].read_text(encoding="utf-8"))
            self.assertEqual(manifest["status"], "FAIL")
            self.assertIn("preflight unavailable", manifest["reason"])
            self.assertIn("failure.txt", manifest["artifact_hashes"])

    def test_pre_calibration_stops_after_resolving_one_executable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = self.main_args(root)
            args.mode = "pre-calibration"
            args.source_root.mkdir()
            pipeline = args.standards_root / "tools" / "style_pipeline.py"
            pipeline.parent.mkdir(parents=True)
            pipeline.write_text("# test fixture\n", encoding="utf-8")
            source = {
                "commit": "locator-only",
                "slug": "source-clean",
                "dirty": False,
                "diff": "",
            }

            def pass_phase(gate: run_gate.Gate, name: str, *_: object, **__: object) -> None:
                gate.phases.append({"name": name, "status": "PASS"})

            def retain_smoke(gate: run_gate.Gate) -> None:
                gate.phases.append({"name": "retain-smoke-evidence", "status": "PASS"})

            with (
                mock.patch.object(run_gate, "parse_args", return_value=args),
                mock.patch.object(run_gate, "source_identity", return_value=source),
                mock.patch.object(
                    run_gate,
                    "preflight",
                    return_value={"dependency": {"dirty": False}},
                ),
                mock.patch.object(
                    run_gate,
                    "resolve_actionlint",
                    return_value=(root / "actionlint", {}),
                ),
                mock.patch.object(
                    run_gate,
                    "initialize_msvc_environment",
                    return_value={"status": "not-required"},
                ),
                mock.patch.object(
                    run_gate.Gate,
                    "run_phase",
                    autospec=True,
                    side_effect=pass_phase,
                ),
                mock.patch.object(run_gate.Gate, "refresh_configured_preflight"),
                mock.patch.object(
                    run_gate.Gate,
                    "retain_smoke",
                    autospec=True,
                    side_effect=retain_smoke,
                ),
                mock.patch.object(
                    run_gate,
                    "resolve_benchmark_executable",
                    return_value=args.build_dir / "benchmark" / "vnm_plot_benchmark",
                ) as resolve_executable,
            ):
                self.assertEqual(run_gate.main(), 0)

            resolve_executable.assert_called_once_with(args.build_dir, args.config)
            manifest_path = next(args.build_dir.rglob("gate_manifest.json"))
            attempt_dir = manifest_path.parent
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["mode"], "pre-calibration")
            self.assertEqual(manifest["status"], "PRE_CALIBRATION_READY")
            self.assertNotEqual(manifest["status"], "PASS")
            self.assertEqual(
                [phase["name"] for phase in manifest["phases"]],
                [
                    "style",
                    "actionlint",
                    "configure",
                    "build",
                    "ctest",
                    "retain-smoke-evidence",
                ],
            )
            self.assertFalse((attempt_dir / "calibration").exists())
            self.assertNotIn("calibration_proposal", manifest["inputs"])
            self.assertFalse(list(attempt_dir.rglob("owner_approval.json")))

    def test_pre_calibration_smoke_does_not_gate_on_git_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = self.main_args(root)
            args.mode = "pre-calibration"
            smoke = args.build_dir / "benchmark" / "smoke-reports" / "native" / "run"
            smoke.mkdir(parents=True)
            raw = smoke / "inspector_benchmark_test.json"
            raw.write_text(
                json.dumps(
                    {
                        "metadata": {
                            "warmup_frames": 0,
                            "measured_frames": 0,
                            "build_source_dirty": "false",
                            "build_source_diff_sha256": "diff",
                            "source_diff_sha256": "diff",
                            "source_dirty": "false",
                            "source_tree": "worktree-content",
                            "build_dependency_dirty": "false",
                            "dependency_dirty": "false",
                            "build_source_commit": "different-commit",
                            "build_source_tree": "different-git-tree",
                            "source_commit": "different-commit",
                            "build_dependency_commit": "different-dependency",
                            "dependency_commit": "different-dependency",
                            "env.GALLIUM_DRIVER": "unset",
                            "env.LP_NUM_THREADS": "unset",
                        }
                    }
                ),
                encoding="utf-8",
            )
            trace = smoke / "benchmark_phase_trace_test.jsonl"
            trace.write_text("validated by mock\n", encoding="utf-8")
            run_gate.write_json(
                smoke / "smoke_invocation.json",
                {"renderer_environment": {}},
            )
            run_gate.write_json(
                smoke / "smoke_validation.json",
                {
                    "status": "PASS",
                    "artifact": raw.name,
                    "artifact_sha256": run_gate.sha256_file(raw),
                    "phase_trace": trace.name,
                    "phase_trace_sha256": run_gate.sha256_file(trace),
                },
            )

            attempt = root / "attempt"
            (attempt / "preflight").mkdir(parents=True)
            source = {
                "commit": "locator-only",
                "dirty": False,
                "diff": "",
                "diff_sha256": "diff",
                "git_tree": "git-tree-locator",
                "exact_tree_sha256": "worktree-content",
            }
            gate = run_gate.Gate(
                args,
                attempt,
                source,
                {
                    "dependency": {
                        "commit": "dependency-locator",
                        "dirty": False,
                    },
                    "environment": {},
                },
            )
            with mock.patch.object(run_gate, "parse_and_validate_phase_trace"):
                gate.retain_smoke()
            self.assertEqual(gate.phases[-1]["status"], "PASS")

    def test_full_gate_rejects_missing_calibration_proposal(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args = self.main_args(root)
            args.mode = "full"
            args.source_root.mkdir()
            pipeline = args.standards_root / "tools" / "style_pipeline.py"
            pipeline.parent.mkdir(parents=True)
            pipeline.write_text("# test fixture\n", encoding="utf-8")
            source = {
                "commit": "locator-only",
                "slug": "source-clean",
                "dirty": False,
                "diff": "",
            }

            def pass_phase(gate: run_gate.Gate, name: str, *_: object, **__: object) -> None:
                gate.phases.append({"name": name, "status": "PASS"})

            with (
                mock.patch.object(run_gate, "parse_args", return_value=args),
                mock.patch.object(run_gate, "source_identity", return_value=source),
                mock.patch.object(
                    run_gate,
                    "preflight",
                    return_value={"dependency": {"dirty": False}},
                ),
                mock.patch.object(
                    run_gate,
                    "resolve_actionlint",
                    return_value=(root / "actionlint", {}),
                ),
                mock.patch.object(
                    run_gate,
                    "initialize_msvc_environment",
                    return_value={"status": "not-required"},
                ),
                mock.patch.object(
                    run_gate.Gate,
                    "run_phase",
                    autospec=True,
                    side_effect=pass_phase,
                ),
                mock.patch.object(run_gate.Gate, "refresh_configured_preflight"),
                mock.patch.object(run_gate.Gate, "retain_smoke"),
                mock.patch.object(
                    run_gate,
                    "resolve_benchmark_executable",
                    return_value=args.build_dir / "benchmark" / "vnm_plot_benchmark",
                ),
            ):
                self.assertEqual(run_gate.main(), 1)

            manifest_path = next(args.build_dir.rglob("gate_manifest.json"))
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["status"], "FAIL")
            self.assertIn("did not produce", manifest["reason"])


if __name__ == "__main__":
    unittest.main()
