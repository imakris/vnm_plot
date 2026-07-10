# vnm_plot implementation plan

Plan date: 2026-07-10

Execution record updated: 2026-07-10

Source review: `VNM_PLOT_ARCHITECTURE_AND_STACKING_REVIEW.md` at `vnm_plot@2d94f01`

## Outcome

Proceed in five gated stages:

1. close the remaining CI/style baseline;
2. instrument, capture a native baseline, then remove known unchanged-frame work in the same renderer/benchmark batch;
3. consolidate ownership, status, query, range, frame-planning, and Qt publication contracts;
4. settle the stack contract and sampling strategy with a private benchmark A/B experiment;
5. land one complete end-to-end stack feature and run final gates.

The product stack implementation does not begin until Stages 2–4 pass and the owner accepts the complete stack contract. The separately authorized dependency Windows repair may run in parallel with local `vnm_plot` work once dependent consumer CI and local product gates are green, but it remains a final-merge gate.

Claude was unavailable for the stated 85-minute review window. Three Codex workers provided cross-worker review; under governance this is not different-model independence. Local commits and review branches may be pushed and PRs may be opened to run CI and gather review. No batch is merged to `master` until the repository owner/human or a different model such as Fable independently reviews it. Each batch receives two review rounds plus its executable gate; require a third round when round two finds a material issue or the remediation materially changes the reviewed behavior.

## Failure ledger

The architecture review records **19 failed or terminated executions** in [Observed failures and current gate status](VNM_PLOT_ARCHITECTURE_AND_STACKING_REVIEW.md#observed-failures-and-current-gate-status). Plan authoring added one failed `apply_patch` verification: a combined patch expected text that no longer exactly matched the staged draft. Codex `/root` owned the authoring error and recovered by rereading the file and applying smaller exact hunks, bringing the total to **20** before implementation began.

Batch 1B then added two failed style executions, bringing the running total to **22**:

1. The aggregate `style_pipeline.py --write` invocation against the three originally named files advanced beyond the 14 switch fixes and attempted 737 insertions/826 deletions. Review caught `fix_hanging_indent.py` deleting a ternary expression's `?`, true arm, and `:` in `sample_statement_for_offset()` before commit. Codex `/root` owned the unsafe invocation, rejected the output, and restored all generated changes through `apply_patch`; no corrupting change was retained.
2. After applying only the authorized 14 switch fixes, the canonical no-write pipeline passed its first stage and failed at stage two on four previously hidden `else if` layout violations. A read-only audit then showed 22 of 27 rule groups red; those counts are diagnostic/provisional because earlier ordered fixes can affect later checks.

Preserve all ledgers rather than replacing them with later green results.

| Current state | Owning batch |
|---|---|
| Dependency Windows workflow failed before creating jobs | Startup failure recovered by `vnm_msdf_text@b9c216a`; real Windows jobs were created. At the 2026-07-10 execution-record update, Linux/macOS/FreeBSD and the normal Windows job were green while Windows full export was still running in [run 29090524058](https://github.com/imakris/vnm_msdf_text/actions/runs/29090524058). Retain the original failure and close Batch 1A only after the terminal four-platform result is recorded. |
| Style baseline | The original 14 switch violations are recovered in local checkpoint `3fe78e6`. The short-circuiting pipeline exposed broader pre-existing governed debt and an unsafe `fix_hanging_indent.py` rewrite; expanded Batch 1B remains open. |
| Benchmark cold dynamic timeout unresolved | Codex `/root` performance-harness owner; Batch 2 instruments it and the ledger stays open after Batch 2 if cause remains unresolved |
| `vnm_plot` eight first-attempt CI failures | Recovered on attempt 2; retain as closed evidence |
| MSVC initialization, local Qt platform, report-validation, and plan-authoring command failures | Recovered/documented; keep in final handoff |

## Fixed rules

- Owner repositories track `master`. Do not pin `vnm_msdf_text` to a tag or commit. Record its resolved commit in build/benchmark artifacts only.
- Preserve the current data/layout/RHI/Qt target boundaries and D3D11 prepare-before-pass/record-in-pass rule.
- Reuse existing series registration, profiler, snapshot, and planner patterns before adding new public types.
- No compatibility aliases for APIs removed because their semantics are unsafe or ambiguous.
- No stack-only metadata, planner, shader, public API, counter, or report field lands unused.
- Performance decisions require full-frame evidence on a real recorded QRhi backend; microbenchmarks and Null QRhi are diagnostic only.
- Every failed or terminated gate remains visible with cause, recovery, and owner even if a later rerun passes.
- Every batch is reviewed in two rounds, with a third round after a material round-two finding/remediation, independently approved by the owner/human or a different model, and passes its named gate before merge.
- The implementation agent is authorized to create local commits, push in-scope review branches, and open PRs. Prefer PR review over direct `master` pushes; do not force-push shared branches or rewrite published history.
- The approved removals of `Data_source_ref`, its raw-reference builders/setters, and ambiguous `Data_source::snapshot()` are intentional breaking API changes. Do not add compatibility aliases or no-op-deleter bridges; provide migration notes and consumer-smoke evidence in the owning batch.

## Responsibilities

These assignments apply unless the owner changes them before a batch starts.

| Batches | Implementation executor | Decision approver / independent reviewer | Gate recorder | Failure-remediation owner |
|---|---|---|---|---|
| 1A | Codex `/root`, only after sibling-repository authorization | Repository owner; Fable or human review before merge | Codex `/root` | Batch 1A executor |
| 1B, 2 | Codex `/root` | Repository owner; Fable or human review before merge | Codex `/root` | Active batch executor |
| 3A–3D | Codex `/root` | Repository owner for API/oracle decisions; Fable or human review before each merge | Codex `/root` | Active batch executor |
| 4A–4B | Codex `/root` for decision record/prototype | Repository owner chooses contract and A/B winner; independent review verifies evidence | Codex `/root` | Batch 4 executor |
| 5 | Codex `/root` | Repository owner plus independent Fable/human review | Codex `/root` | Batch 5 executor |

### Recorded owner authorization

On 2026-07-10 the repository owner authorized the implementation agent to create local commits, push review branches, and open PRs for the in-scope work. This authorization does not waive batch gates, independent review, branch protection, or the prohibition on destructive history rewriting.

The owner also approved:

- D1–D3 and D5–D10 using the recommended positions below;
- the intentional removal of `Data_source_ref`, the raw-reference API, and ambiguous `Data_source::snapshot()` without compatibility aliases;
- automatic generation of the fixed benchmark scenarios and numeric noise-margin proposal after baseline calibration, followed by owner review of the generated manifest before Checkpoint 2.2.

## Decisions required

The owner-approved decisions below are product oracle subject to their executable evidence gates. D4 alone remains unresolved until the Stage 4 A/B experiment and visual review.

| ID | Decision | Approved contract | Status and remaining evidence |
|---|---|---|---|
| D1 | Negative values | Algebraic cumulative stacking; a negative component descends from the previous cumulative value, preserving `h=f+g`. | **Approved 2026-07-10.** Retain visible cancellation/negative verification. |
| D2 | Defined domain | Intersection of all enabled component domains; no left extrapolation; right extension only through explicit hold-forward. | **Approved 2026-07-10.** Retain overlap, disjoint, and hold verification. |
| D3 | Group topology | Optional `stack_group_id` on ordinary series; ascending series ID is order; callers use `apply_series_updates` when topology must change atomically. Individual add/remove may render intermediate membership. | **Approved 2026-07-10.** Caller-disciplined atomicity is the product contract. |
| D4 | Sampling strategy | Unresolved between bounded exact event union and bounded shared grid. Ship exactly one, not both. | **Open.** Stage 4 A/B native-backend and visual evidence; owner selects the winner. |
| D5 | Scalar/style contract | Contribution always uses scalar `get_value`; at least two enabled members; individual `NONE` members contribute invisibly; all-NONE and custom-layer groups fail explicitly. | **Approved 2026-07-10.** Retain the status-table tests. |
| D6 | Busy/failure contract | BUSY reuses only a prior complete structurally matching group wholesale; FAILED suppresses/diagnoses; EMPTY is empty; generations never mix. | **Approved 2026-07-10.** Retain streaming/counter verification. |
| D7 | Floating timestamp API | Require integral nanoseconds in the member-pointer hot path and provide checked floating-to-ns conversion at ingestion. | **Approved 2026-07-10.** Typed/erased UBSan and performance verification remain required before Batch 3B closes. |
| D8 | Duplicate timestamps | Highest physical source index wins regardless of traversal direction. | **Approved 2026-07-10.** Retain ascending/descending duplicate tests. |
| D9 | Pixel-parity indicators | Renderer evaluates cursor time from the held published frame and returns immutable values with one-frame latency. | **Approved 2026-07-10.** Retain published-frame identity tests. |
| D10 | Unchanged-frame reuse | Stable nonzero `current_sequence()` plus stable/non-conservative access semantics authorizes reuse; zero/unstable/conservative inputs rebuild and report why. | **Approved 2026-07-10.** The existing second-frame upload oracle may change after Checkpoint 2.1 retains its baseline. |

## Execution automation contract

The implementation should make each batch reproducible through one gate runner and machine-readable manifests rather than a collection of undocumented shell commands. The exact script layout may follow repository conventions established in Batch 2, but it must implement these contracts when the corresponding capability first becomes necessary:

1. **Preflight:** record source commit/diff identity, compiler, CMake, Qt, Python, external standards revision/hash, graphics backend/device/driver, dependency commit, relevant environment, and complete invocation. Missing tools such as `actionlint` are explicit environment-bootstrap failures, not missing product specifications. Bootstrap tools with a recorded version/hash rather than relying on an unversioned global installation.
2. **Gate result:** emit a versioned manifest containing batch/checkpoint, start/end time, command, exit status, phase, inputs, artifact hashes, CI run/job URLs, and recovery relationship. Failed or terminated attempts are append-only; a green rerun never overwrites them.
3. **Artifacts:** write raw local evidence below the active build tree's `gate-artifacts/<batch>/<source-identity>/<timestamp>/` directory and upload the same bundle from CI/PR runs. Do not commit raw timing output to the source tree.
4. **Delivery:** local commits and review-branch pushes are allowed before gates close so CI and reviewers can inspect them. Merge to `master` is the protected event and requires the named batch gate and independent approval.
5. **Review rounds:** run two review/remediation rounds. Run a third when the second round identifies a material correctness, API, cache, performance, or evidence defect, or when its remediation materially changes reviewed behavior.
6. **Backend matrix:** deterministic native smoke uses D3D11 on Windows, Metal on macOS, Vulkan and desktop OpenGL on Linux where available, and desktop OpenGL on FreeBSD. A backend unavailable on its intended runner is reported explicitly rather than silently replaced. Null QRhi remains diagnostic only. Raw timing comparisons use one accepted machine/backend/build fingerprint; a fingerprint mismatch invalidates comparison.
7. **Calibration generation:** generate scenario manifests from the fixed plan matrices, dimensions, seeds, warm-up counts, and seven-run protocol. Preserve every raw run. Do not remove outliers automatically; an interrupted or failed run is retained and a replacement run is a separately identified attempt.
8. **Calibration arithmetic:** deterministic counters whose accepted baseline is zero use exact-zero regression rules. For positive metrics, calculate relative median drift from the two calibration sets as specified below. If a median is zero/nonpositive or below declared measurement resolution, report relative margin as unavailable and propose a resolution-based absolute rule instead. The runner must not silently turn unstable calibration into a permissive threshold; it emits `CALIBRATION_REVIEW_REQUIRED` with the observed drift/resolution and proposed rule for owner approval.
9. **A/B evidence:** mechanically reject a D4 candidate that violates correctness or M/V/H bounds. For surviving candidates, generate a numerical comparison, deterministic screenshots, pixel differences, and a side-by-side visual bundle. The runner may recommend but never silently select the D4 winner.
10. **Conditional stops:** the Stage 3C source-eligibility failure and any proposed Batch 3E remain owner decisions because they change supported-source scope or public API. Automation must present the failing evidence and the two plan-authorized choices without choosing one.

## Stage 1 — Close the baseline

### Batch 1A — Repair dependency Windows workflow

Execution status recorded 2026-07-10:

- the smallest workflow-only fix landed on dependency `master` as `vnm_msdf_text@b9c216a`;
- the invalid job-level `runner.temp` use was replaced by step-level `RUNNER_TEMP` initialization exported through `GITHUB_ENV`;
- dependency Linux, macOS, and FreeBSD passed at `b9c216a`; Windows created both real jobs and its normal job passed, while its full-export job remained in progress in run `29090524058` at the time of this record;
- all eight dependent `vnm_plot@2d94f01` text-OFF/text-ON jobs passed on attempt 2;
- `actionlint` is not installed locally; retain its versioned bootstrap/validation as environment evidence rather than treating it as a missing project definition.

Scope:

- separately authorized sibling file `C:\plms\bsd_licensed\vnm_msdf_text\.github\workflows\ci-windows.yml`;
- no `vnm_plot` production-source change;
- retain `master` tracking.

Actions:

1. Run `actionlint C:\plms\bsd_licensed\vnm_msdf_text\.github\workflows\ci-windows.yml`, recording the actionlint version/hash outside the repository.
2. Inspect expression-context availability, especially job-level `runner.temp`.
3. Make the smallest workflow-only correction.
4. Push the sibling workflow correction to `master`.
5. Require dependency Linux, Windows, macOS, and FreeBSD workflows to pass and Windows to create real jobs.
6. Rerun dependent `vnm_plot` workflows if the dependency contents—not only workflow syntax—change.

Gate:

- dependency four-platform CI green;
- `vnm_plot` text-OFF/text-ON CI green on Linux, Windows, macOS, and FreeBSD;
- resolved dependency commit recorded, never pinned.

### Batch 1B — Formatting-only baseline migration

Initial checkpoint files:

- `src/core/series_renderer.cpp:95`;
- `src/core/types.cpp:47-53,389-394`;
- `tests/test_msdf_lcd_shader_reference.cpp:217-225`.

The original review observed only the first failing pipeline stage because the pipeline stops at the first nonzero checker. Once those 14 violations were fixed, the next stage and a read-only all-rule audit proved the baseline is broader. The batch remains formatting-only, but its scope expands to checker-enumerated C/C++ files as each canonical rule becomes active.

#### Checkpoint 1B.1 — Original switch debt

- change only the 14 reported case-layout/indentation violations;
- keep provenance: five arrived in `2d94f01`, nine predate it.
- retained local commit: `3fe78e6`;
- gate evidence: switch checker green, `git diff --check` green, initialized Release build green, CTest 21/21 green.

#### Checkpoint 1B.2 — Newly exposed `else if` debt

- apply only the four token-preserving `else if` to `else` newline `if` layout fixes reported at:
  - `src/core/layout_calculator.cpp:756,1024`;
  - `src/qt/plot_widget.cpp:1197`;
  - `include/vnm_plot/core/types.h:562`;
- rerun the switch and unbraced-control-flow checks, `git diff --check`, initialized Release build, and CTest 21/21.

#### Checkpoint 1B.3 onward — Ordered rule migration

Process exactly one canonical style rule at a time in pipeline order:

1. retain the no-write checker's enumerated findings and external standards identity;
2. run only that rule's fixer against its enumerated files, never the aggregate pipeline `--write`;
3. require a lexical C/C++ token-equivalence check for every formatting rewrite before build/test; any intended token change requires an explicit plan amendment and leaves this batch;
4. inspect and retain the complete diff, then rerun all preceding rules plus the active rule;
5. run `git diff --check`, the initialized Release build, and CTest 21/21;
6. obtain the iterative three-worker and Claude Fable review required for the batch, feeding findings into the next remediation/review round;
7. continue until the complete canonical pipeline passes without `--write`.

The canonical check command is:

  ```powershell
  python C:\plms\varinomics\varinomics-standards\tools\style_pipeline.py `
    --root C:\plms\bsd_licensed\vnm_plot
  ```

Record standards repository base `f5edc8b` and `style_pipeline.py` SHA-256 `8715313C94D8DCC4257EB3792459BA8D7C759C9CEB6958958B64560FD65CB2F4` with the initial gate because the standards repository is external and moving. Repin both after any standards-tool repair.

`fix_hanging_indent.py` is quarantined: an isolated replay proved that its operator-return renderer corrupts the `std::string{}` ternary in `tests/test_msdf_lcd_shader_reference.cpp::sample_statement_for_offset()`. Do not run that fixer again until the standards repository has a focused regression test and corrected implementation, or until the affected findings are satisfied by manually reviewed token-equivalent edits. A later green result does not erase the failed aggregate-write execution.

Gate:

- governed style pipeline passes from its first stage;
- initialized Release build passes;
- CTest passes 21/21.

Parallel rule: Batch 1A remains a final-merge gate, but it does not block local Batch 1B/2 work while dependent `vnm_plot` CI and local product gates are green. Batch 1B must pass before semantic `vnm_plot` changes are pushed for review.

## Stage 2 — Establish evidence, then remove unchanged-frame work

### Batch 2 — One governed hot-path batch with two local checkpoints

Both checkpoints touch the same renderer, benchmark, counters, and tests. They remain one delivery batch/PR; the native baseline checkpoint must pass before the optimization checkpoint starts.

Primary files:

- `include/vnm_plot/rhi/series_renderer.h`;
- `include/vnm_plot/core/types.h` and access-policy contract documentation;
- `src/core/series_renderer.cpp`;
- benchmark ring buffer/source, CLI, reporting, profiler, CMake, and `benchmark/ARCHITECTURE.md`;
- QRhi lifecycle and benchmark helper tests;
- relevant workflows.

Hypothesis and full-path boundary:

- hypothesis: compiling out test tracing and reusing stable primary/LINE buffers removes library CPU staging and GPU upload without reducing live producer throughput or worsening full-frame latency outside calibrated machine noise;
- boundary: static frame entry through QRhi GPU finish, and live generator publication through snapshot/plan/upload/GPU finish, including allocations, source locks, and producer wait.

#### Checkpoint 2.1 — Instrument and retain the baseline

1. Add an explicit graphics-backend option. Native creation hard-fails on Null unless Null was explicitly requested.
2. Record `QRhi::backend()`, device/driver, build type, text mode, dimensions, seed, scenario, finish state, and resolved dependency commit.
3. Record exact source-tree hash plus local diff/commit identity, compiler, Qt version, and complete invocation with every raw artifact so Checkpoint 2.1 remains reproducible after Checkpoint 2.2 changes the same delivery batch.
4. Separate cold startup, untimed warm-up, submission timing, and GPU-finished timing.
5. Repair benchmark sequence semantics: ring occupancy and monotonic revision are separate; `clear()` empties occupancy and increments rather than resets revision. Snapshot and `current_sequence()` expose the same stable revision.
6. In the same batch, document the public contract: stable nonzero `current_sequence()` is required for reuse; zero/unstable disables it; access-semantic identity must cover callable behavior; a custom layer still receives `prepare()` even when library-owned buffers are reused.
7. Report primary sample, LINE-window, uniform, known custom, and total observable upload bytes/counts. Do not add stack/composition fields before Stage 5 consumes them.
8. Report snapshots and bytes/time, order-scan samples, allocations, producer wait, published samples/second, dropped/overwritten samples, memory high-water, planning time, output count, and full-frame p50/p95/p99.
9. Enable benchmark helper tests and add a short deterministic native-backend CI smoke. Hosted CI gates counters/pixels, not raw timing.
10. Instrument the unresolved cold dynamic timeout by backend creation, shader/pipeline creation, first submission, frame count, producer lock wait, and shutdown.

Measurement protocol:

- use one named machine/backend/build and retain all raw artifacts;
- use fixed static Bars and live Trades scenarios plus 1/8/64 ordinary LINE series at fixed dimensions/seeds;
- run two independent baseline calibration sets, each with two untimed warm-up runs and seven measured runs;
- compare run-level medians and frame-level p50/p95/p99, GPU-finished time, allocations, producer wait, published samples/second, dropped/overwritten samples, and memory;
- derive each metric's noise margin from the maximum relative median drift between the two calibration sets and record measurement resolution;
- the gate runner generates the exact scenario manifest and proposed per-metric relative/absolute rules from retained calibration artifacts using the execution automation contract;
- before Checkpoint 2.2, the repository owner reviews and approves that generated manifest: lower-is-better positive metrics require `after <= baseline * (1 + approved_noise_margin)`, while positive producer throughput requires `after >= baseline * (1 - approved_noise_margin)`. Deterministic-zero and sub-resolution metrics use the generated exact/absolute rules. No later batch may replace these rules without a plan amendment.

Checkpoint gate:

- fixed-seed native smoke and sequence-reset tests pass;
- public/header contract review and stable/zero/conservative/custom-layer API tests pass;
- Null cannot masquerade as GPU evidence;
- both calibration sets and the approved numeric rule are retained;
- cold timeout has a source-confirmed cause or remains explicitly open with phase evidence;
- the recorded D10 approval authorizes the existing unchanged-second-frame oracle change after Checkpoint 2.1 retains the baseline.

#### Checkpoint 2.2 — Remove test work and unchanged uploads

1. Compile test-only vectors/writes only with `VNM_PLOT_ENABLE_TEST_HOOKS`.
2. Add `sample_upload_key` using source, stable nonzero sequence, selected LOD, origin, access semantics, window/spans, policies, and view kind.
3. Add a primary-content revision incremented on every primary upload.
4. Key padded LINE data by primary-content revision, interpolation, and spans—not allocation generation.
5. Stable/non-conservative unchanged frames reuse library buffers even when a custom layer requests a current CPU snapshot.
6. Before Batch 3A removes raw references, a `Data_source_ref` without an owner disables cross-frame reuse and records `nonowning_source`; an owning key retains the shared owner/control-block identity plus object pointer so allocator address reuse cannot false-hit.
7. Zero/unstable/conservative semantics rebuild and record the disable reason.
8. Preserve custom-layer `prepare()` and report its observable traffic separately.

Deterministic gate:

- stable second frame: zero library primary/LINE restaging/upload;
- changed LINE frame: one primary and one LINE-window upload, each once;
- style without LINE: only its required primary upload;
- zero/unstable/conservative input: rebuild plus disable-reason counter;
- non-owning source: rebuild plus `nonowning_source` reason;
- pixels and command order unchanged.

End-to-end gate:

- repeat the approved seven-run protocol with identical inputs;
- deterministic counter win is present;
- every regression metric satisfies the owner-approved numeric noise rule;
- all raw before/after artifacts remain attached to the batch.

## Stage 3 — Consolidate existing contracts before stacking

Stage 3 has four independently useful batches. None introduces stack-only public API.

### Batch 3A — Owning sources and explicit snapshot statuses

Primary files:

- `include/vnm_plot/core/types.h`;
- `include/vnm_plot/core/basic_series_builder.h`;
- examples, benchmark source/window, and focused tests;
- every ordinary and preview source assignment.

Hypothesis: ownership/status changes move lifetime checks to descriptor publication without increasing render-thread snapshot time or producer wait. The full-path boundary is descriptor publication through snapshot consumption and teardown.

Actions:

1. Replace `Data_source_ref` in ordinary and preview descriptors with `shared_ptr<Data_source>`.
2. Remove raw-reference setters/builders without a no-op-deleter bridge.
3. Make `Benchmark_data_source` own/share its `Ring_buffer`; changing only the source pointer type would leave a teardown UAF.
4. Remove ambiguous `Data_source::snapshot()` and migrate its three production callers to explicit `try_snapshot()` handling.
5. EMPTY means no data; BUSY preserves the current operation; FAILED diagnoses/suppresses; READY retains its hold.
6. Remove renderer snapshot-hold storage that becomes redundant once complete snapshot results are held canonically.

Gate:

- `rg "Data_source_ref|set_data_source_ref|data_source_ref\("` has no public, production, example, or benchmark hits;
- Ubuntu Clang ASan (`-fsanitize=address`) ordinary/preview lifetime and benchmark teardown pass;
- Ubuntu Clang TSan core/update-remove stress is run with `-fsanitize=thread`; if Qt/QRhi cannot link or run under TSan, record `UNSUPPORTED` with compiler/linker evidence and run the same stress test without sanitizer on all four CI platforms;
- four-status tests pass;
- repeat the approved Stage 2 full-path protocol; source wait and latency satisfy its numeric rule.

### Batch 3B — One evaluator and one range producer

Primary files:

- `include/vnm_plot/core/algo.h` and access/query types;
- `src/core/types.cpp`;
- `src/core/auto_range_resolver.cpp`;
- `src/qt/plot_widget.cpp`;
- focused range/interaction tests.

Prerequisite: the recorded owner approvals for D7, D8, and D9 satisfy the semantic decision gate. Their named UBSan, performance, duplicate, and published-frame evidence remains required. Existing duplicate/indicator tests are not changed before Batch 3B establishes that evidence.

Hypothesis: one canonical evaluator preserves render semantics while indexed sources eliminate full LOD-0 UI copies/scans; the measurement boundary is cursor/auto-adjust request through immutable result publication.

Actions:

1. Add one `resolve_sample_at` producer for ordering, interpolation, duplicate winner, hold, and nonfinite semantics.
2. Preserve GLOBAL value-only/range-only access without timestamps in canonical `Data_source::query_v_range()`.
3. Time-bounded VISIBLE and point queries still require timestamp access.
4. Route auto-adjust and indicators through canonical producers.
5. Delete the private range scanner only after value-only/all-time parity is proven.
6. For pixel-parity indicators, the UI publishes cursor time and the renderer returns immutable values from the held rendered frame; accept one-frame latency.
7. Resolve the floating timestamp decision D7 in this batch so stacking does not inherit undefined conversion.
8. Replace duplicate local time constants and `floor_div` with the canonical time-unit implementation; update README query/indicator contracts with the same API change.

Gate:

- existing GLOBAL value-only test remains green;
- unordered `[0,90,10,100]`, query 11 resolves nearest timestamp 10;
- ascending, descending, duplicates (highest physical index wins), nonfinite policies, hold, and all statuses pass;
- indicator equals the rendered frame and never a newer source generation;
- UBSan floating timestamp boundary cases pass;
- duplicate scanner is absent only after parity;
- direct indexed million-sample cursor/auto-adjust work is bounded and satisfies the Stage 2 numeric regression rule; fallback-source cost remains reported.

### Batch 3C — Shared frame plan and range/render identity

Primary files:

- `src/core/series_window_planner.*`;
- new internal `frame_series_planner.*` only if it immediately replaces current production planning;
- frame range and renderer paths;
- focused cache/layout tests.

Hypothesis: one shared plan and attempted-LOD cache removes duplicate snapshots/scans and range/render disagreement without increasing acquisition cost as retained history grows. The full-path boundary is frame planning entry through range result and renderer-consumed plan.

Actions:

1. Use full pre-layout framebuffer width as the deterministic LOD-budget width; final usable width changes transforms, not selected LOD.
2. Produce one immutable per-series/per-view frame plan consumed by VISIBLE range and rendering.
3. Share only complete `snapshot_result_t` values through a frame cache keyed `(Data_source*, LOD)`; cache every attempted level, not only the winner, and retain holds for the frame. Do not cache `query_time_window()` by that key because its result also depends on time range, access semantics, interpolation, hold, and nonfinite policy; leave direct query results uncached until evidence justifies their complete key.
4. Make `Series_view_plan` own one `sample_window_t`; delete duplicated window fields and migrate renderer/tests atomically.
5. Key renderer geometry reuse with existing immutable descriptor fields, owning source identity, stable access semantics, and concrete plan/window data. Do not add registry-incarnation or geometry-revision concepts.
6. Update README VISIBLE/frame-plan behavior with the production change.

Gate:

- VISIBLE range and renderer use the same LOD, sequence, window, and one snapshot;
- ten series sharing one source/LOD call `try_snapshot()` once per frame;
- attempted-LOD cache counters prove each `(source,lod)` acquisition occurs once per frame;
- at fixed W/visible interval, run 1x/10x/100x retained histories for direct/zero-copy, copy-on-snapshot, and unknown-order sources after a changed sequence: exact snapshot bytes/physical visits at 100x must not exceed the 1x value, and time/producer-lock metrics must satisfy the approved Stage 2 noise rule for any source class eligible for stacking; fallback classes that fail remain measured but are not silently declared stack-safe;
- warning-clean initialized build and all focused tests pass;
- the approved Stage 2 protocol passes for full-frame latency, allocations, snapshot bytes/time, alignment scans, producer wait, and memory.

Stop condition: if a source class required for stacking fails the fixed-history-growth gate, stop Stage 3 and let the owner choose either an explicit stack-ineligible source status or a plan amendment. Only an approved amendment may create named Batch 3E with exact bounded-window-hook files, API oracle, source migrations, and gates; no unnamed hook or scaffolding exists in the current plan.

### Batch 3D — Qt publication and update semantics

Primary files:

- `src/qt/plot_widget.cpp`;
- `src/qt/plot_renderer.cpp`;
- Qt synchronization/cache tests;
- matching README configuration example.

Hypothesis: an existing-map publication revision eliminates unchanged map/config copying without changing renderer geometry identity or frame output. The boundary is Qt synchronize entry through immutable map publication.

Actions:

1. Add only a series-map publication revision for Qt copying.
2. In one `apply_series_updates` lock, null entries erase their IDs and add/update/remove commit together; increment the map revision exactly once for the batch.
3. Individual add/remove remains a committed intermediate topology; callers use the batch API when stack membership must change atomically.
4. Copy map/config only when its publication revision changes.
5. Geometry equality continues to use source identity, access semantics, and plan/window fields; global map revision, color, and label do not invalidate geometry.
6. Conservative callable access without a stable semantics key still invalidates safely.
7. Update the README example to use the canonical widget setter for widget-owned fields.

Gate:

- one initial map/config copy, then zero until mutation;
- one mixed add/update/remove call publishes one revision and contains no null map entries;
- color/label-only stable-semantic update causes no snapshot/range scan/geometry upload;
- conservative callable clone invalidates;
- pixel/command output is unchanged;
- approved Stage 2 synchronization/full-frame metrics satisfy the numeric rule.

## Stage 4 — Settle the stack design with evidence

### Batch 4A — Contract decision record

The execution record already approves D1–D3 and D5–D6. Batch 4A converts those decisions into the complete reviewed stack contract and leaves only D4 open. D3 uses `std::optional<int> stack_group_id` with ascending existing series ID and caller-batched topology; do not add a group descriptor. The record also fixes:

- component order;
- scalar/range projection;
- duplicate winner;
- nonfinite and hold behavior;
- BUSY wholesale reuse key;
- singleton, all-NONE, and custom-layer failures;
- GLOBAL/GLOBAL_LOD common-domain behavior;
- cross-source/cross-LOD non-atomic snapshot wording;
- preview and indicator behavior.

Do not add product semantic tests before approval. Private benchmark prototypes may assert only mechanical bounds, checked arithmetic, and explicit failures; they do not settle product semantics.

### Batch 4B — Sampling A/B experiment

In this batch, extend the internal planner request with separate `render_width_px` and optional `max_visible_samples`. Ordinary series pass no cap and retain the existing closest-to-one-pixel `choose_lod_level()` behavior; focused irregular-ladder and coarsest-over-budget tests require identical ordinary LOD/window/pixels. The private stack paths pass B/N without overloading shader width.

Build two private compositor functions in the existing benchmark using that canonical selector/evaluator, plus one benchmark-private QRhi upload/draw adapter and shader shared by both candidates with the simple interleaved layout intended for Stage 5. This makes native GPU-finished and visual A/B evidence real without adding a public interface, strategy enum, planner hierarchy, or target. Before Batch 4B closes, delete the losing function, its mechanical checks, configuration, and documentation; retain the winning benchmark path, raw artifacts, and decision record until Stage 5 promotes it.

Common definitions:

- `W`: full framebuffer width;
- `K`: enabled members;
- `C`: internal ordinate-pairs-per-pixel budget, initially 2 for the experiment;
- `D`: 1 for all-LINEAR, 2 when STEP_AFTER requires paired events;
- `M=floor(C*W)`: per-group/per-view hard band-ordinate-pair cap;
- `A=align_up(sizeof(actual_prototype_stack_uniform_std140), rhi->ubufAlignment())`, with a static assertion for the real record layout—not a guessed 64 bytes;
- `U`: actual uniform blocks updated for that group/view;
- `H_limit=checked_add(checked_mul(40,M), checked_mul(A,U))`: conservative per-group/per-view upload cap for the simple interleaved layout, group-shared metadata, padded boundaries, and aligned uniforms;
- `V=sum(physical_input_visits)+K*E`: composition visits, hard-capped at `2*M`.

Actual prototype upload bytes are `12*K*E + 8*group_span_count + 8*sum_line(span_length+2) + sizeof(actual_prototype_stack_uniform_std140)*U`; upload only real uniform bytes, not alignment padding. Paired equal-timestamp events are stored in left/right vector order, so no GPU event-side stream exists. H_limit/memory reservation uses `A*U`. With `K*E<=M`, base is <=12M, group-shared spans <=4M, and padded LINE data <=16M; 40M leaves explicit alignment headroom. Span records must remain group-shared; if formats or ownership change, re-derive H_limit before use. Use checked add/multiply/divide for `D*K*K`, `D*K`, `K*E`, `A*U`, M, H, V, and every byte count. Overflow, V>2M, or actual bytes>H_limit returns an explicit budget status. Main, preview, and multiple groups each have their own M/H/V result; frame totals are reported as sums, never treated as one group's allowance.

Both prototypes call the same benchmark-local wrapper around the production LOD/window selector with explicit `max_visible_samples`; only composition differs. “Complete input” and V count interpolation padding, duplicate-collapse visits, hold samples, and every physical sample examined across attempted and selected LODs. The frame cache retains every attempted `(Data_source*,LOD)` result, not only the winning level.

Candidate A — budgeted event union:

- `B=floor(M/(D*K*K))`, require B>=2;
- shared selector chooses each source-local LOD independently with physical input visits <=B;
- event union exact relative to selected curves;
- STEP pairs are emitted consecutively in left/right vector order at the same timestamp;
- `E<=D*K*B`, `K*E<=M`.

Candidate B — bounded shared grid:

- `N=floor(M/(D*K))`, require N>=2;
- shared selector chooses each source-local LOD independently with physical input visits <=N;
- `E<=D*N`, `K*E<=M`;
- STEP changes use consecutive left/right vector-order events at the sampled grid transition;
- grid approximation and step displacement are explicit.

A/B matrix:

- K={2,8,32}; W={800,3840};
- LINEAR, STEP_AFTER, mixed interpolation;
- phase-shifted grids, narrow spikes, duplicates, gaps, negative cancellation;
- unrelated LOD counts/scales;
- static and live sources;
- native backend, identical Release build/finish/instrumentation.

Record:

- selected LOD/input counts;
- compose CPU time, physical visits, and V;
- output pairs/vertices;
- base/boundary/metadata/uniform/total bytes;
- allocations and memory;
- producer wait and snapshot cost;
- full-frame p50/p95/p99 and GPU-finished time;
- human visual verdict for spikes, steps, gaps, and cancellation.

Decision gate:

- uncapped ordinary series retain identical selected LODs, windows, and pixels on regular/irregular ladders and coarsest-over-budget cases;
- both candidates respect M, V, and H_limit or return an explicit budget status;
- both repeat the approved Stage 2 seven-run native-backend protocol with identical workloads and candidate-specific B/N caps;
- choose exactly one strategy using performance plus visual evidence;
- record why the loser was rejected;
- source search proves the losing function, flags, mechanical checks, and docs are absent before Batch 4B closes;
- do not expose C or a strategy switch unless evidence proves applications need it.

## Stage 5 — Land one complete stack feature

### Batch 5 — Atomic end-to-end feature delivery

This is one governed delivery batch even if developed as reviewable local commits. Do not merge or deliver public metadata, an unused planner, or an unused shader separately.

Hypothesis and full-path boundary: the selected bounded strategy renders the owner-approved cumulative function within per-group/view M/V/H limits, preserves non-stack behavior, and keeps main/preview producer throughput within the Stage 2 numeric rule. Measure from source publication through frame planning, composition, uploads, GPU finish, preview, and frame-published indicator results.

Scope:

- move/promote the winning benchmark compositor and shared QRhi adapter/shader into the canonical layout/RHI targets, switch the benchmark to the production producer, and delete all benchmark-local winner copies in the same commit;
- `std::optional<int> stack_group_id` on ordinary descriptors, ascending existing series ID as order, and existing `apply_series_updates` for atomic caller-intended topology;
- selected bounded composition planner;
- VISIBLE range consumes every bottom/top extremum from the exact same immutable composed plan used by rendering;
- bounded scalar GLOBAL/GLOBAL_LOD range: intersect effective member domains, query scalar min/max at each source-local selected level, accumulate every prefix lower/upper in `double`, cache by domain/levels/sequences/semantics, and report fallback scan work;
- frame cache and complete structural/data cache keys;
- one group VBO containing contiguous per-member interleaved `{float t_rel,bottom,top}` blocks (`static_assert(sizeof==12)`) and one band shader; do not start with a split shared-time layout;
- AREA bottom/top bands;
- DOTS cumulative knots;
- LINE cumulative boundaries using a separately keyed padded `{t_rel,top}` buffer and four prev/p0/p1/next bindings;
- stack/composition/upload observations first consumed by this feature;
- preview membership/order from ordinary descriptors, while main and preview independently plan their effective sources/access/styles/LODs; a missing/failed preview member suppresses only the complete preview group, never main and never a partial preview;
- frame-published cumulative indicators;
- extend the existing `examples/preview_config` executable rather than add another example/QML target;
- focused math/RHI/interaction tests;
- representative benchmark scenarios and documentation.

Cache rules:

- base composition requires stable nonzero sequences and non-conservative/explicit access semantics for cross-frame reuse;
- unstable/conservative inputs recompute within M/H and record the disable reason;
- BUSY may reuse only a prior complete structural match wholesale;
- reuse keys use existing immutable descriptor fields, owning source identity, access semantics, and concrete plan/window fields—no new registry-incarnation/geometry-revision scheme;
- color/label changes do not recompose;
- boundary data key is base-content revision, LINE-membership mask, and spans;
- color, width, opacity, and pixel-snap changes update uniforms/draw state only and never upload boundary data;
- adding/removing LINE style changes boundary membership and may rebuild boundary data, not base geometry;
- changed LINE data composes once, uploads base once, and uploads padded boundary once.

Correctness gate:

- all owner-approved contract cases pass;
- negative cancellation includes intermediate cumulative range;
- VISIBLE range/render plan identity proves cancellation and every intermediate cumulative extremum cannot be clipped;
- common-domain EMPTY and hold behavior pass;
- Candidate-specific STEP behavior has no diagonal bridge;
- preview and indicator match the published rendered representation;
- main and preview LODs remain independent;
- failed/empty preview group never suppresses main and never renders partial preview;
- unordered input, failed member, custom layer, missing scalar, singleton, all-NONE, and budget failures are explicit;
- no partial member is silently dropped within one immutable topology.

Performance gate:

- define the actual stack std140 uniform record first, static-assert its layout, compute A from its real size/backend alignment, and use checked arithmetic for every per-group/per-view M/V/H bound;
- observed upload bytes equal `12*K*E + metadata + 8*sum_line(span_length+2) + sizeof(actual_stack_uniform_std140)*U`; aligned reservation uses A*U, and both remain within their checked H_limit/memory bounds;
- zero steady allocation after warm-up where cache reuse is permitted;
- stable unchanged second frame: zero composition/base/boundary upload;
- output, composition visits, snapshot bytes, alignment visits, and producer locks satisfy the Stage 3C bounded-history gate;
- GLOBAL and GLOBAL_LOD repeat the 1x/10x/100x fixed-domain history gate; a fallback scalar range scan that grows with history returns explicit `STACK_GLOBAL_RANGE_UNBOUNDED`/ineligible status unless a separately approved bounded direct-range contract exists;
- non-stack counters are bit-for-bit unchanged;
- repeat the approved Stage 2 raw-run protocol; full-frame p50/p95/p99, GPU-finished time, bytes, draws, allocations, snapshots, producer wait, and memory satisfy its numeric rule;
- introduce a split shared-time/ordinate layout only through a later measured plan amendment if the simple one-VBO layout fails an accepted bytes/p95 target.

Visual gate:

- human approval of positive, negative, cancellation, mixed STEP/LINEAR, gap, preview, and unrelated-LOD examples before any screenshot becomes a golden oracle.

## Final release gate

Before the feature merge/release is considered complete:

1. governed style pipeline passes;
2. initialized Release build and full CTest pass;
3. shader bake/resource lifecycle passes on every supported backend;
4. dependency and `vnm_plot` Linux/Windows/macOS/FreeBSD workflows pass;
5. clean no-sibling configure/install/consumer smoke passes while fetching dependency `master`;
6. benchmark metadata names actual backend and resolved dependency commit;
7. deterministic and end-to-end performance gates pass;
8. two or three review rounds plus owner/human or different-model independent review recheck every contract/cache/performance claim;
9. all observed failures, including recovered ones, remain in the handoff;
10. worktree contains only the intended batch changes.

## Recommended immediate next action

The Batch 1A startup repair has landed; monitor its remaining Windows full-export job and record the terminal four-platform result. Locally, start with Batch 1B, then Batch 2; do not touch product stack code before the evidence and contract stages pass. Review branches may be pushed, but no batch is merged while Batch 1A remains red or incomplete. This is the shortest path that produces trustworthy evidence without idling local work.
