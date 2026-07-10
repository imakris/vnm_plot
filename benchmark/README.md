# vnm_plot benchmark evidence

`vnm_plot_benchmark` has two separate backend controls:

- `--backend qrhi|qrhi-offscreen` selects the presentation path.
- `--graphics-backend native|d3d11|metal|vulkan|opengl|null` selects the
  graphics API for diagnostics. Retained evidence requests `native` and accepts
  whichever non-Null API Qt selects. No particular Vulkan, OpenGL, D3D, Metal,
  or future graphics API is a product requirement. Null is available only when
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

Snapshot accounting distinguishes logical `benchmark.snapshot.view_bytes` from
physical `benchmark.snapshot.copied_bytes`; direct ring views require the latter
to remain exactly zero. Render-thread CPU allocation count/bytes cover the full
measured frame while excluding the benchmark profiler's own map/sample storage;
`renderer.frame.gpu_buffer_allocation_*` separately counts QRhi buffer creation.

The phase trace is flushed at cold setup, backend creation, every warmup and
measured frame boundary, generator shutdown, and completion. If an external
timeout kills a run, its last complete phase remains available.

## Calibration protocol

`tools/calibrate.py` generates six scenarios: static Bars and live Trades, each
with 1, 8, and 64 ordinary LINE series. Every series owns an independent ring
and data source; the generator publishes the same deterministic sample to each
ring. This avoids nested direct-view locks while preserving per-series renderer
work and total publication counters.

Static scenarios contain 10,000 deterministic samples per series; live
scenarios publish 1,000 samples per series per second so the fixed 100,000
sample rings remain below overwrite capacity during the measured workload. GPU
buffer growth and order scans in a changing live source are measured and
calibrated rather than declared deterministic zeros. The corresponding static
scenario counters, including measured publications, remain exact-zero rules
across every retained run. The runner
first executes and retains a native environment probe, then binds every new or
resumed run to its source, executable, dependency, machine, OS/CPU, Qt,
device, driver, and actual backend fingerprint.

The fixed protocol performs two calibration sets. Each set has two complete
untimed warm-up runs followed by seven measured runs. Every run itself has two
runner warm-up frames followed by 120 measured, GPU-finished frames at 1200x720
with text enabled. Across six scenarios this is 24 warm-up plus 84 measured
executions; the separate native environment probe is diagnostic and is not part
of those 108 scenario executions. It writes `scenario_manifest.json`, every raw run artifact
and phase trace, and `proposed_noise_margins.json`:

```powershell
python benchmark\tools\calibrate.py `
  --executable build-text\benchmark\vnm_plot_benchmark.exe `
  --output-dir benchmark-results\calibration
```

Generated manifests and margins are labeled `CALIBRATION_REVIEW_REQUIRED`; the
runner does not promote its own calibration into an acceptance threshold. Each
attempt has a unique directory, failures are retained, and recovery reuses only
an artifact whose full source, executable, device, backend, dependency, and
scenario fingerprint validates. Relative margins are the exact maximum relative
median drift between the two sets. Designated deterministic zeros use exact-zero
rules; nonpositive, sub-resolution, or unstable observations remain explicitly
review-required.

## Checkpoint gate

`tools/run_gate.py` is the append-only checkpoint entry point. A full run checks
the external style pipeline and versioned `actionlint`, configures and builds,
runs CTest (including native pixel/counter validation), retains the latest smoke
native smoke attempt, and runs the fixed calibration protocol:

```powershell
python benchmark\tools\run_gate.py `
  --source-root . `
  --build-dir build-text `
  --checkpoint 2.1
```

The full runner ends with `CALIBRATION_REVIEW_REQUIRED` and prints the retained
proposal SHA256. It cannot approve its own output. After the repository owner
has inspected that exact proposal, the owner records approval against its hash:

```powershell
python benchmark\tools\approve_gate.py `
  --attempt-dir <retained-gate-attempt> `
  --proposal-sha256 <reviewed-sha256> `
  --approved-by <owner-identity>
```

Only that explicit second step can transition a complete, clean checkpoint gate
to `PASS`. CI smoke packaging is labeled `DIAGNOSTIC_PASS` and is never a
checkpoint approval.

On Windows the full runner initializes the Visual Studio x64 environment when
the current shell has not done so. It resolves pinned `actionlint` 1.7.12 from
`PATH`, `--actionlint`, or `<build>/tools/actionlint-1.7.12/actionlint.exe` and
records its version and SHA-256; an absent or differently versioned executable
is an explicit environment-bootstrap failure.

Every invocation creates a new
`<build>/gate-artifacts/batch-2/<source-identity>/<timestamp>/` directory. Its
versioned gate manifest records preflight identity, complete commands, phase
results, recovery linkage, CI coordinates, and hashes for every retained
artifact. Failed and timed-out attempts are never replaced. CI uses
`--mode ci-retain` to package the same validated smoke evidence layout for
artifact upload without rerunning the build.
