# vnm_plot benchmark evidence

`vnm_plot_benchmark` has two separate backend controls:

- `--backend qrhi|qrhi-offscreen` selects the presentation path.
- `--graphics-backend native|d3d11|metal|vulkan|opengl|null` selects the
  graphics API. `native` never falls back to Null. Null is available only when
  it is requested explicitly.

For repeatable runs, prefer an exact frame count:

```powershell
build-text\benchmark\vnm_plot_benchmark.exe `
  --backend qrhi-offscreen `
  --graphics-backend native `
  --static --data-type bars --render-style line `
  --series-count 8 --seed 42 `
  --warmup-frames 2 --frames 120 --finish `
  --scenario static-bars-line-8 `
  --output-dir benchmark-results
```

Each successful run writes a human-readable `.txt` report, a raw `.json`
artifact, and `benchmark_phase_trace.jsonl`. The raw artifact retains the
invocation, source commit/tree/diff identity, dependency commit, compiler build
type, Qt version, requested and actual QRhi backend/device, text/finish state,
dimensions, seed, scenario, frame output count, raw observations, and calculated
p50/p95/p99 values. High-rate metrics use deterministic reservoir sampling with
the retained limit recorded in the artifact; aggregate count/total/min/max are
not sampled.

The phase trace is flushed at cold setup, backend creation, every warmup and
measured frame boundary, generator shutdown, and completion. If an external
timeout kills a run, its last complete phase remains available.

## Calibration protocol

`tools/calibrate.py` generates six scenarios: static Bars and live Trades, each
with 1, 8, and 64 ordinary LINE series. Every series owns an independent ring
and data source; the generator publishes the same deterministic sample to each
ring. This avoids nested direct-view locks while preserving per-series renderer
work and total publication counters.

The default protocol performs two calibration sets of seven runs. Each run has
two untimed warmup frames followed by 120 measured, GPU-finished frames at
1200x720 with text enabled. It writes `scenario_manifest.json`, all raw run
artifacts and phase traces, and `proposed_noise_margins.json`:

```powershell
python benchmark\tools\calibrate.py `
  --executable build-text\benchmark\vnm_plot_benchmark.exe `
  --output-dir benchmark-results\calibration
```

Generated manifests and margins are labeled
`proposed-owner-approval-required`; the runner does not promote its own
calibration into an acceptance threshold. Interrupted runs are resumable only
when the retained artifact has the requested measured-frame count.
