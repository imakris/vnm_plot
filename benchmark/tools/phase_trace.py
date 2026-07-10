#!/usr/bin/env python3
"""Strict validation for retained benchmark phase traces."""

from __future__ import annotations

import json
import math
from typing import Any


def expected_phases(warmup_frames: int, measured_frames: int) -> list[tuple[str, int]]:
    return [
        ("cold.setup.begin", 0),
        ("cold.setup.end", 0),
        ("cold.backend_create.begin", 0),
        ("cold.backend_create.end", 0),
        ("warmup.begin", 0),
        ("warmup.end", 0),
        ("measure.begin", 0),
        ("measure.end", 0),
        ("shutdown.generator.begin", 0),
        ("shutdown.generator.end", 0),
        ("complete", 0),
    ]


def parse_and_validate_phase_trace(
    text: str,
    warmup_frames: int,
    measured_frames: int,
) -> list[dict[str, Any]]:
    if warmup_frames < 0 or measured_frames < 1:
        raise RuntimeError("phase trace frame counts are invalid")
    records: list[dict[str, Any]] = []
    previous_elapsed = -math.inf
    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line:
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            raise RuntimeError(
                f"phase trace line {line_number} is malformed JSON"
            ) from exc
        if not isinstance(record, dict):
            raise RuntimeError(f"phase trace line {line_number} is not an object")
        elapsed = record.get("elapsed_ms")
        frame = record.get("frame")
        phase = record.get("phase")
        if (
            not isinstance(elapsed, (int, float))
            or isinstance(elapsed, bool)
            or not math.isfinite(float(elapsed))
            or float(elapsed) < previous_elapsed
        ):
            raise RuntimeError(
                f"phase trace line {line_number} has invalid elapsed_ms"
            )
        if not isinstance(frame, int) or isinstance(frame, bool):
            raise RuntimeError(f"phase trace line {line_number} has invalid frame")
        if not isinstance(phase, str):
            raise RuntimeError(f"phase trace line {line_number} has invalid phase")
        previous_elapsed = float(elapsed)
        records.append(record)

    expected = expected_phases(warmup_frames, measured_frames)
    actual = [(record["phase"], record["frame"]) for record in records]
    if actual != expected:
        raise RuntimeError(
            f"phase trace sequence mismatch: expected {expected}, got {actual}"
        )
    return records
