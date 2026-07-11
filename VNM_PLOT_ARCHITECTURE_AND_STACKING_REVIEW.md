# vnm_plot architecture and performance-first stacking review

Review target: `vnm_plot` at `2d94f0184cdfb099d8d8aafae5a0d955f03566ee`

Review date: 2026-07-10
Scope: existing architecture, correctness, duplication, performance, and a first-class stacked-series design for independently sampled and independently levelled data sources.

## Normative authority and 2026-07-11 convergence

This document records review evidence and rationale. The sole normative product
contract is the "Decisions required" register in
`VNM_PLOT_IMPLEMENTATION_PLAN.md`. The owner-approved D11-D15 and amended D2,
D6, D7, D8, and D9 entries supersede earlier proposals here for
`Published_state`, a `Frame_orchestrator` component, retained frame snapshots,
snapshot capability/batch APIs, `Data_source::identity()`, registry
incarnations, `geometry_revision`, series-instance tokens, and the split v1 GPU
layout. The accepted follow-up left D4 unresolved; later exact executable
refinements are proposals pending owner ratification in the plan register and
must not be presented here as approved product oracle. Stage 4 may implement
both candidates only through private/internal evidence paths; the owner must
select D4 before any Stage 5 public stack exposure.

The plan has one consolidated ratification record,
`RU-contract-ratification-record/A1`. It runs only after the owner disposes all
eight pending proposals—P-S1, P-D7, P-Q1, P-D6, P-C1, P-D2, P-R1, and P-D15—
in a dependency-consistent set: P-R1 may be ratified only with P-C1 ratified
before or in the same disposition. Its exact documentation hash must pass three
reviews before any affected implementation. It is not split or reused. A1
numbers RU-4A source actions if and only if P-C1, P-D2, P-R1, and P-D15 are all
owner-ratified consistently; otherwise it records the RU-4A stop and numbers no
RU-4A action. Every individual proposal disposition remains recorded for a
later separately reviewed amendment. D4 remains a later nondelegated evidence
decision outside A1.

## Executive conclusion

**Failure headline: 19 failed or terminated executions were observed—nine CI jobs/workflows, one style gate, five local/environment diagnostics, and four report-validation commands; their provenance, recovery, and ownership are grouped below.**

`vnm_plot` has a sound foundation and does not need a rewrite. The data/layout/RHI/Qt target split, immutable render-boundary descriptors, held snapshot lifetimes, per-source LOD planning, nanosecond timestamps, and D3D11-compatible prepare/record split should be preserved. The highest-value work is to make the current frame plan the single producer for range, rendering, interaction, and the proposed stack composition; remove avoidable steady-state snapshots, CPU staging, and GPU uploads; and tighten source lifetime and status contracts.

The requested stack should be a core frame-planning capability, not a custom rendering layer and not an application-side precomputed `h` source. Each component selects its LOD independently. The planner then evaluates those source-local curves under a strict screen-space composition/output budget and emits cumulative bottom/top bands. Two bounded strategies remain viable: an event union exact for the selected curves, or a shared bounded screen grid with better per-input density but another approximation. Stage 4 may implement both privately for native-backend/visual evidence; the owner selects one before Stage 5 public activation, not before private implementation. Both support unrelated timestamps/LOD ladders and bound composition/GPU output; current snapshot/window APIs may still copy, lock, or align against more history, so those costs must be measured and, if necessary, given a bounded source hook before stacks are enabled by default.

The recommended implementation order is:

1. Completed during review: publish the `vnm_msdf_text` contract to `master`, rerun the live integration, and preserve both the initial red and subsequent green evidence.
2. Establish truthful production counters and remove unchanged-frame uploads and test-only render-loop work.
3. Consolidate frame snapshots, range planning, point evaluation, source statuses, and ownership.
4. Add the bounded stack planner and its mathematical tests.
5. Add the stacked-band RHI path, Qt interaction/preview integration, and end-to-end performance gates.

No proposed feature should land on the strength of a microbenchmark alone. The acceptance gate must include full-frame p50/p95/p99, total GPU upload bytes, allocation count, snapshot/query work, producer wait time, and native-backend identity.

## Review method and governance

This review followed `varinomics-standards`, including claim classification, concrete remediation for every finding, observable verification, explicit reporting of failed gates, test-oracle-first sequencing, and performance evidence that spans the real end-to-end path.

Four historical model reviews were used, followed by a separate Codex claim audit and local verification:

- three xhigh Codex reviews covering architecture, data/render mechanics, and falsification;
- one different-model Claude Fable review launched with model `claude-fable-5`, read-only repository tools, and no MCP tools;
- a final Codex claim audit over the combined findings and competing stack designs;
- a local source trace, builds, tests, style gate, CI observation, and benchmark inspection.

External review artifacts remain outside this repository. The listed historical
Fable session is evidence for this review only; its availability/exclusion and
its model vote do not govern implementation review policy. Current review units,
exact source/evidence-identity three-xhigh loops, architecture lens, and
ambiguity-only Fable use are defined solely by
`VNM_PLOT_IMPLEMENTATION_PLAN.md`.

Claims are labelled as follows:

- **Source-confirmed**: directly established by the cited code, build, test, CI, or benchmark output.
- **Inferred**: a consequence supported by the source but not yet measured in a representative production workload.
- **Proposed**: a design or acceptance contract that requires owner approval before implementation.
- **Unresolved**: evidence is insufficient for a production conclusion; the next measurement is stated.

### Final claim-audit corrections

The final Codex claim audit confirmed the principal findings and made four useful scope corrections that this synthesis adopts:

1. The duplicate auto-range scanner is real redundancy, but a normal valid policy does **not** execute both scanners in one request. The remedy is consolidation and drift prevention, not a claim of a measured double scan.
2. Policy-pointer invalidation and per-frame Qt map copying are source-confirmed mechanisms; their production severity remains unmeasured. Their batches therefore require replacement-heavy and 1/100/1000-series measurements before priority is raised.
3. Upload evidence must distinguish sample geometry from complete QRhi traffic. The renderer should report primary, LINE-window, stack, uniform, and known custom-layer bytes separately plus a total; if a custom layer cannot expose bytes, the total must be labelled incomplete rather than guessed.
4. A budgeted exact union is internally consistent only when “exact” is explicitly limited to the independently selected LOD functions and LOD selection enforces the input budget before union. A full-width grid per member grows with membership, while the proposed bounded shared grid partitions one fixed output budget and can retain more input density for large groups. The exact formulas live only in the plan register. The default is therefore **unresolved** pending native-backend compose/bytes/p95 and visual spike/step A/B evidence.

The audit also confirmed that commit `e605b5f` deliberately removed LOD hysteresis. This report therefore does not reintroduce it.

## Existing architecture to preserve

These are positive findings, but each has a concrete preservation rule so subsequent work does not accidentally erase the benefit.

| Evidence | Assessment | Concrete preservation rule and verification |
|---|---|---|
| `CMakeLists.txt:286-366,415-478,553-580` | **Source-confirmed:** data, layout, RHI, and Qt Quick are separate link targets. | Put stack mathematics and evaluation in layout/core, QRhi resources in RHI, and UI storage/interaction in Qt. Add a dependency-direction CMake smoke that builds each narrow target; do not introduce Qt types below the Qt target. |
| `include/vnm_plot/core/types.h:294-352` | **Source-confirmed:** snapshots explicitly retain storage lifetime. | D12 retains one selected acquisition only through consume-scoped window/extrema/staging/custom prepare, then destroys the hold before another same-source acquisition and before record. Add a source whose storage dies immediately after release and verify derived-plan ASan safety plus writer progress. |
| `src/qt/plot_widget.cpp:167-196` | **Source-confirmed:** series descriptors are cloned before crossing to the render side. | Keep each published descriptor/map snapshot immutable. Put membership on that descriptor and document `apply_series_updates` as the required atomic topology path; individual add/remove calls commit and may render an intermediate topology, so callers requiring atomic group changes must use the batch API. |
| `include/vnm_plot/rhi/series_renderer.h:63-78`, `src/qt/plot_renderer.cpp:467-523` | **Source-confirmed:** resource upload/prepare occurs before the pass and record occurs inside it, including D3D11 constraints. | Stack buffers must be created and uploaded during prepare and only bound/drawn during record. Exercise the shader/resource smoke on every supported backend. |
| `src/core/series_window_planner.cpp:229-246,860-923` | **Source-confirmed:** LOD is chosen per source from that source's scale ladder. | Stack planning must request one independent plan per component. Tests must use deliberately unrelated LOD counts and scales and assert no equality of LOD indices is assumed. |
| `include/vnm_plot/core/types.h`, `include/vnm_plot/rhi/series_renderer.h:81-105`, `src/core/series_renderer.cpp:1723-1734` | **Source-confirmed:** CPU timestamps remain signed 64-bit nanoseconds and are rebased for fp32 GPU use. | Keep timestamp merge/comparison in `int64_t`; rebase only the final visible group buffer. Test values near both signed limits without subtracting in a way that overflows. |

A broad dependency-injection system, plugin framework, source-tree rewrite, or container replacement has no demonstrated payoff and should not be undertaken. If profiling later identifies a specific registry or allocation bottleneck, change only that producer and repeat the end-to-end gate.

## Observed failures and current gate status

There were **19 failed or terminated executions** during this review. They are grouped here so a later successful command cannot hide them.

### Resolved live-`master` integration failure: eight initial CI jobs

All eight first-attempt jobs from the four workflows failed during configure/build:

- Linux: <https://github.com/imakris/vnm_plot/actions/runs/29081126739>
- Windows: <https://github.com/imakris/vnm_plot/actions/runs/29081126724>
- macOS: <https://github.com/imakris/vnm_plot/actions/runs/29081126707>
- FreeBSD: <https://github.com/imakris/vnm_plot/actions/runs/29081126754>

Both text-OFF and text-ON first attempts failed because `vnm_plot` required an LCD contract that was not yet present on fetched `vnm_msdf_text` `master`: text-OFF could not find `vnm_msdf_text/include/vnm_msdf_text/lcd_contract.h`, and text-ON could not find target `vnm_msdf_text::lcd_contract`. The required sibling commit `c03a350` (`Commonize LCD contract and package smokes`) was subsequently pushed to that repository's `master` during the review.

Concrete recovery and result:

1. `vnm_msdf_text@c03a350` was pushed to `master`.
2. All four workflows were rerun as attempt 2; all eight configurations passed on 2026-07-10.
3. Keep both `GIT_TAG master` declarations at `CMakeLists.txt:203-218`; this repository intentionally uses the owner's dependency `master` as a live integration oracle.
4. Configure from a checkout with no sibling dependency present, then run install/package-consumer smoke tests so local directory leakage cannot mask the fetched `master` state.
5. Record the resolved dependency commit in CI artifacts for diagnosis and benchmark comparison, without pinning it.

The dependency repository's own `c03a350` push workflows passed on [Linux](https://github.com/imakris/vnm_msdf_text/actions/runs/29083205733), [macOS](https://github.com/imakris/vnm_msdf_text/actions/runs/29083205779), and [FreeBSD](https://github.com/imakris/vnm_msdf_text/actions/runs/29083205767). Its [Windows workflow](https://github.com/imakris/vnm_msdf_text/actions/runs/29083204930) historically failed at workflow level with zero jobs and no logs. That historical cause was the invalid job-level `runner.temp` use; the focused correction subsequently passed all dependency platforms at `vnm_msdf_text@b9c216a`. Batch 0 is closed. This paragraph preserves failure provenance and creates no active remediation instruction.

### Repository style gate: one failed execution

The governed style pipeline stopped on 14 violations. Five in `test_msdf_lcd_shader_reference.cpp` were introduced by `2d94f01`; the other nine in `series_renderer.cpp`/`types.cpp` predate that commit but remain perpetuated and exposed by the current tree:

- `src/core/series_renderer.cpp:95`;
- `src/core/types.cpp:47,49,51,53,389,391,393,394`;
- `tests/test_msdf_lcd_shader_reference.cpp:217,219,221,223,225`.

Concrete recovery: change only the reported switch/case layout and indentation, rerun the complete style pipeline from its first stage, and keep the formatting-only patch separate from semantic work. Do not mix this cleanup into stack implementation.

### Local/environment diagnostics: five failed or terminated executions

1. A first direct MSVC build invocation failed in standard-library headers because the compiler environment was not initialized. The governed `vcvarsall.bat x64` invocation then built successfully and CTest passed 21/21. Concrete recovery is to use the standards-provided initialized command in local documentation/automation rather than interpreting the first failure as product source breakage.
2. One `QT_QPA_PLATFORM=offscreen` run passed 20/21; `PlotInteractionItem` failed because the local Qt deployment contained only the Windows platform plugin. The same test passed with `QT_QPA_PLATFORM=windows`. Concrete recovery is to deploy the offscreen plugin where offscreen is required or select an actually installed platform in the local Windows gate; CI should keep exercising its intended headless platform.
3. One cold dynamic benchmark exceeded 30 seconds and was terminated; warmed repetitions completed. Cause is **unresolved**. Instrument backend creation, first shader/pipeline creation, first submitted frame, frame count, producer lock wait, and generator shutdown before changing production code. Add a bounded startup timeout with a phase-labelled failure to the harness.
4. `gh run view --log-failed` for the dependency Windows workflow returned failure because the workflow created no jobs/log archive. Query run/job metadata first; treat an empty job list as workflow validation failure instead of requesting nonexistent logs.
5. An unauthenticated web fetch of the same private/uncached run returned a cache-miss error. Use the authenticated GitHub CLI/API and local workflow validation for this repository; do not infer a cause from the failed page fetch.

### Report-validation diagnostics: four failed executions

Two independently written mechanical source-reference checks hit the same PowerShell interpolation parse error in the check command itself (`$path:$line` and `$p:$end`). A later stale-wording grep also had a malformed PowerShell double-quoted pattern because a Markdown backtick escaped its closing quote. The first staged `git diff --check` then rejected two Markdown hard-break trailing spaces at the report header. Their corrected forms parsed 53 references in the final primary check with none invalid; the differently tokenized proof check parsed 41 citation groups with every endpoint valid; the corrected single-quoted grep found no stale wording; and the final cached diff check passed. Concrete prevention: retain explicit braced interpolation such as `${path}:${line}`, single-quote literal grep patterns containing Markdown backticks, and use blank lines rather than trailing-space hard breaks.

The corrected local command built successfully and CTest passed **21/21 tests in 1.60 seconds**. The build also emitted C4459 shadow warnings for `k_ns_per_ms` and `k_ns_per_second` in `include/vnm_plot/core/algo.h:717-718` and `src/qt/plot_widget.cpp:1277`, shadowing canonical constants in `time_units.h`. The concrete fix is to remove the local duplicates and use the canonical constants, then enable this warning as an error for project code after the existing warnings are cleared.

Historical failure ownership is explicit: dependency publication and the four
`vnm_plot` workflow reruns completed; dependency Batch 0 closed at `b9c216a`;
style Batches 1B/1C closed at `vnm_plot@2ec8013`; Batch 2 owns the shadow
warnings; local gate automation owns initialized MSVC/platform selection; and
the benchmark-integrity batch owns cold-start instrumentation. Later green
evidence does not erase the initial failures or their provenance.

### 2026-07-10 Batch 2 execution addendum

The original review above is preserved as architectural evidence. Batch execution
status, candidate hashes, failed hypotheses, retained run identifiers, and
recovery history live in `VNM_PLOT_IMPLEMENTATION_PLAN.md`; they are not repeated
here.

The retained benchmark requests Qt's native QRhi backend, rejects Null evidence,
and records the actual backend/device without making Vulkan, OpenGL, D3D, Metal,
or any future API a product requirement. It records source, dependency,
executable, Qt, machine, OS/CPU, renderer environment, and complete invocation
identity. Unix hosted validation uses Xvfb where a native window-system context
requires it. Platform-specific compile and link requirements remain guarded by
their actual availability.

The calibration architecture fixes six scenarios and 108 scenario executions,
plus a separate current-environment probe. It uses 10,000 static samples,
requires every retained deterministic-zero value to be zero, validates artifact
hashes and aggregate terminal phase traces, and separates logical zero-copy view
bytes from physical copied bytes. Render-thread CPU allocations and QRhi buffer
creation are distinct counters. Producer statistics are reset inside an exact
paused epoch, updated outside the writer mutex, and the publication schedule is
rebased before measurement so warm-up debt cannot enter live throughput.

One phase-trace stream remains open for a run. Eleven aggregate success
boundaries are flushed outside the measured frame loop; a frame-specific record
is emitted only on failure. Measured-frame metadata and output counters
independently enforce exact workload completion. Windows native file operations
adapt deep logical artifact paths to extended-length paths without changing
commands, metadata, hashes, recovery relationships, or proposal semantics.

The runner can end only in retained failure or
`CALIBRATION_REVIEW_REQUIRED`. A separate owner action may record checkpoint
PASS only after inspection of the exact generated proposal and approval of its
SHA-256 on clean source and dependency identities. Superseded calibration
artifacts cannot approve the checkpoint.

## Prioritized architecture and performance findings

### P0 — Historical live-`master` delivery-order failure; closed at dependency `b9c216a`

**Classification: Historical source-confirmed delivery failure, recovered under an owner-approved policy.** Both fallback declarations deliberately use the owner's moving `master`; the first attempts correctly exposed that the consumer reached `master` before the required dependency contract did. After `c03a350` reached dependency `master`, all eight dependent `vnm_plot` attempt-2 jobs passed. The dependency Windows workflow was then corrected and all dependency platforms passed at `b9c216a`.

**Resolution:** Batch 0 is closed at `vnm_msdf_text@b9c216a`. The stable policy remains unversioned package discovery and FetchContent `master`, with the resolved dependency commit recorded in evidence and no-sibling/install/package consumption retained.

**Performance relevance:** no direct runtime effect. Because the dependency intentionally moves, every benchmark artifact must record its resolved commit so before/after binaries remain attributable without changing the `master` policy.

### P1 — VISIBLE auto-range does not use the displayed LOD plan

**Classification: Source-confirmed correctness and performance issue.** `Auto_v_range_mode::VISIBLE` says it uses LOD selection (`include/vnm_plot/core/plot_config.h:40-47`), but `src/core/auto_range_resolver.cpp:432-439` selects LOD 0 for every mode except `GLOBAL_LOD`. The renderer chooses the actual visible LOD later in `src/core/series_window_planner.cpp:860-923`. This can scan full-resolution visible data and compute a range that differs from the curve actually displayed.

**Remediation:** introduce one production frame entry before range/layout. It produces one immutable snapshot-free per-series/per-view plan containing status, sequence, selected LOD, source window, drawable spans, extrema, semantics, and normalized derived data. Use full framebuffer width—not post-label `usable_width`—as the deterministic LOD-budget width for both range and renderer; final layout may still use `usable_width` for transforms, but it must not reselect LOD. VISIBLE range, `Series_renderer`, D9 indicators, and the immutable frame result consume the same executed plan. Do not call independent planners or introduce an unbounded layout/replan cycle.

**Verification:** use a source with million-, ten-thousand-, and thousand-sample levels at width 1000. Assert range and renderer consume the same selected LOD and source sequence, and that each `(source, LOD)` is snapshotted once per frame.

### P2 — Unchanged frames restage and upload primary and line-window data

**Classification: Source-confirmed performance issue.** A valid snapshot leads to primary staging/upload in `src/core/series_renderer.cpp:1640-1805`. A custom layer requests a frame snapshot even when sequence and view are unchanged (`src/core/series_renderer.cpp:879-893`; `src/core/series_window_planner.cpp:382-391`), so unchanged data is uploaded. LINE rebuilds and uploads its padded window at `src/core/series_renderer.cpp:1921-2063`. The public profiler counts the primary upload at `src/core/series_renderer.cpp:1368-1435` but omits the line-window upload at `2058-2062`.

**Remediation:** add two explicit keys to each view state:

- `sample_upload_key`: registration slot, owning source owner/object, stable nonzero sequence, selected LOD, origin, access/semantics key, source window, drawable-span hash, nonfinite behavior, hold behavior, and view kind. A zero/unstable sequence or conservative semantics disables cross-frame reuse and increments a reason counter; treating zero as a reusable version risks stale geometry;
- `line_window_key`: primary-content revision incremented on every content upload, interpolation, and segment spans. Allocation generation alone is insufficient because an in-place primary upload changes content without reallocating the VBO.

When the relevant key matches, retain the buffer even if a current CPU snapshot was required for a custom layer. Continue calling custom-layer prepare as its ABI requires. Publish primary, line-window, and total sample upload counts and bytes.

**Verification:** after warm-up, static LINE and LINE+custom-layer frames with stable nonzero sequence/non-conservative semantics must report zero library-owned primary/LINE CPU restaging and uploads, with identical library pixels. An arbitrary custom layer may legitimately upload in each `prepare()`; report that separately and never claim it is zero when unobservable. A data-affecting mutation for LINE must cause one primary upload and one derived line-window upload, each exactly once; styles without LINE perform only their required primary upload. Zero/unstable/conservative inputs must recompute rather than reuse. Change `tests/test_qrhi_layer_lifecycle.cpp:1033-1049`, which currently expects an unchanged second-frame upload, only after the new oracle is owner-approved.

### P3 — Snapshot caching is per series instead of per frame/source/LOD

**Classification: Source-confirmed mechanism; cost is inferred until measured.** Each series state owns its own cache (`include/vnm_plot/rhi/series_renderer.h:147-152`; `src/core/series_window_planner.cpp:248-269`). Multiple series using the same source and LOD can therefore obtain multiple copies and potentially observe different sequences.

**Remediation:** D12 defines each `Data_source` object as one acquisition domain. `try_snapshot`, `time_range`, `query_time_window`, `query_v_range`, proposed `query_sample`, and future lock/snapshot/hold methods are acquiring; metadata, LOD topology, `current_sequence`, `time_order`, and support flags are non-acquiring. RU-3A must publish this contract and atomically remove the current `cached_snapshot_hold` plus every hold-bearing per-series plan/cache/custom/record representation; otherwise that independently mergeable hash would state a false invariant. Its existing per-series planner may still acquire independently, but consumes/releases before another same-source call and before record. RU-3C2 alone later replaces those independent acquisitions with one cross-series/shared-key production scheduler. Distinct objects sharing backing require independent public-acquisition semantics. No duplicate scheduler, capability enum, or domain token is needed.

**Verification:** RU-3A source search proves no surviving plan/cache/context hold;
lock-backed writer progress occurs after per-series derivation/custom prepare and
before record/next acquisition. RU-3C2 separately proves ten series sharing one
source/LOD produce one request and that exactly one scheduler exists. A later
sharing gate cannot retroactively justify a hold-bearing RU-3A hash.

### P4 — UI range and indicator paths duplicate and diverge from renderer semantics

**Classification: Source-confirmed correctness and performance issue.** `auto_adjust_view()` snapshots/scans LOD 0 at `src/qt/plot_widget.cpp:1106-1147`. Indicators snapshot LOD 0 at `1312-1371` and use `bracket_timestamp()`, which infers order from endpoints and binary-searches (`include/vnm_plot/core/algo.h:615-675`) despite `Data_source` supporting `UNORDERED`. These paths also bypass the renderer's complete nonfinite policy.

**Pending P-Q1 rationale:** one point producer avoids the present divergent
ordering, nonfinite, hold, and status behavior. The sole register now owns the
complete request/value types, invariant-safe result factories/failure reasons,
semantic-support versus physical-visit rules, ordered/UNKNOWN/UNORDERED
behavior, exact tie, expected-sequence, default/direct-override, and
no-nested-acquisition rules. In particular, unrelated physically visited
nonfinite history cannot reject a finite chosen NEAREST winner. This review
deliberately does not restate that operative contract. P-Q1 is not
owner-ratified or recorded in review-clean consolidated A1, so RU-3B cannot
begin.

Route `auto_adjust_view()` through the same range producer used by frame auto-range. The stack planner and stacked indicator must consume the frozen rendered representation rather than implement another source query. Approved D9 publishes the cursor request with the executed snapshot-free plan/result and rejects a non-current request. Pending P-C1—not approved D9—separately proposes monotonic activation/movement/deactivation cursor state, current/presented identities, immediate clearing, and absent/failure currency. P-R1 requires P-C1 ratification before or in the same owner disposition, recorded together by review-clean consolidated A1. Do not independently query after render, because the source may have advanced. A direct source query remains valid for non-pixel-parity operations only when selected LOD and expected sequence are supplied; a mismatch discards that invocation without synchronous retry/spin, while a later scheduled request is independent.

**Verification:** unordered timestamps `[0,90,10,100]` queried at 11 resolve to 10. Exact midpoint cases choose the greater timestamp identically for ascending, descending, unordered, negative, wrapped-duplicate, and signed-extreme inputs without overflow. Cover every nonfinite/status/expected-sequence outcome and a million-sample indexed override that proves bounded work and default/direct parity. Tooltip values match the displayed selected LOD. If P-C1 is ratified and recorded by review-clean consolidated A1, its separate verification covers atomic activation/movement/deactivation clear, rapid request orderings, and rejection of delayed old READY/STALE/no-batch/group-failure/section-failure state.

### P5 — `Data_source::snapshot()` erases EMPTY, BUSY, and FAILED distinctions

**Classification: Source-confirmed design issue.** `include/vnm_plot/core/types.h:729-733` converts non-ready outcomes into an empty snapshot. The production users are limited to `src/core/auto_range_resolver.cpp:60` and the Qt paths at `src/qt/plot_widget.cpp:1111,1320`.

**Remediation:** remove the wrapper atomically and migrate each user to `try_snapshot()`:

- EMPTY: valid absence of data;
- BUSY: use D6's `structure_key`-matched wholesale fallback or publish the canonical no-geometry busy failure, and increment a busy observation;
- FAILED: suppress that operation and report once through the configured diagnostic path;
- READY: consume while retaining its hold.

Pending P-S1 in the sole register defines invariant-safe construction and a
top-level sequence independent of payload. In particular, EMPTY nonzero is an
exact observed empty revision while zero means the revision is unavailable or
unstable; zero is not an exact revision and never authorizes reuse. Do not
reread `current_sequence()` after the operation or leave a deprecated alias.
Private construction/storage and read-only status/sequence/contextual/nonowning-
pointer observers keep invalid combinations unrepresentable.

**Verification:** a four-state source must prove no view mutation on BUSY, an observable diagnostic on FAILED, no draw on EMPTY, and normal consumption on READY.

### P6 — Non-owning `Data_source_ref` can outlive its source across the render boundary

**Classification: Source-confirmed lifetime risk.** The raw reference path is stored by `include/vnm_plot/core/types.h:787-829`, used by both preview and ordinary series at `include/vnm_plot/core/types.h:955,1001-1032`, exposed by `include/vnm_plot/core/basic_series_builder.h:67-71`, and cloned for asynchronous rendering without extending source lifetime. Production examples use it, as does the benchmark.

**Remediation:** replace `Data_source_ref` in both `series_data_t` and `preview_config_t` with `std::shared_ptr<Data_source>`, then remove `set_data_source_ref`, `data_source_ref`, and the builder overload in one public API change. Migrate `hello_plot`, `function_plotter`, tests, and benchmark; do not add a no-op deleter compatibility bridge. In the benchmark, make `Benchmark_data_source` own a `shared_ptr<Ring_buffer<T>>` and let generator/window share it. Merely changing the source from `unique_ptr` to `shared_ptr` while it retains `Ring_buffer&` would allow the plot-held source to outlive its buffer during QObject teardown. Shared-pointer copying occurs when immutable descriptors are published, not per sample or draw.

**Verification:** ASan test releases every caller handle after adding ordinary and preview series and renders safely until removal; a benchmark teardown test destroys the window/generator while the render descriptor briefly remains held; TSan stress replaces/removes series during render synchronization. `rg "Data_source_ref|set_data_source_ref|data_source_ref\\("` must return no production, example, benchmark, or public-header use.

### P7 — Floating timestamp-member conversion can invoke undefined behavior

**Classification: Source-confirmed correctness issue.** `include/vnm_plot/core/access_policy.h:63-77` multiplies a floating member by `1e9` and casts directly to `int64_t`. NaN, infinity, or out-of-range finite values make the conversion invalid/undefined. README and typed API tests make floating seconds an adopted behavior, so it cannot simply be ignored.

**Remediation:** D7 exposes public `checked_seconds_to_ns(double) -> optional<int64_t>` for caller ingestion into integral-nanosecond storage. It rounds the exact binary64 mathematical value times `1e9` to nearest, exact halves away from zero, range-checks before conversion, and rejects nonfinite/nonrepresentable values. Pending P-D7 in the sole register owns the exact reference/cv normalization, volatile rejection, integral/non-bool, and signed/unsigned `numeric_limits::digits` admissibility trait; this review does not create a second operative definition.

**Verification:** compile-fail/detection tests reject floating member pointers and migrate existing examples/docs. An exact binary64-rational oracle covers positive/negative integers and halves (`+/-1/1024` seconds is an exact half-nanosecond case after scaling); `std::nextafter` neighbours around both signed rounded-result boundaries identify representable accepted/rejected inputs instead of inventing a one-nanosecond step at that magnitude. NaN/infinities fail under UBSan, and accessor/full-frame evidence proves no hot-loop conversion.

### P8 — Test-only vectors execute in the production draw loop

**Classification: Source-confirmed easy performance win.** Four vectors labelled test instrumentation in `include/vnm_plot/rhi/series_renderer.h:181-186` are cleared per frame (`src/core/series_renderer.cpp:660-667`) and receive four `push_back` calls per draw (`1552-1561`).

**Remediation:** guard members and all writes with the existing `VNM_PLOT_ENABLE_TEST_HOOKS`; build the lifecycle test against a hook-enabled library/object and normal releases without it. Provide a test accessor instead of `private`/`public` preprocessor rewriting.

**Verification:** normal Release objects contain no hook vectors/writes, hook-enabled tests retain their ordering assertions, and production draw/upload output is unchanged.

### P9 — Policy identity and series identity can invalidate too much or reuse the wrong state

**Classification: Source-confirmed invalidation mechanism; allocator-ABA consequence is inferred.** Access cache identity includes policy object address (`include/vnm_plot/core/types.h:102-129,569-581`), while descriptors are cloned on update (`src/qt/plot_widget.cpp:178-193`). Color-only updates can therefore invalidate data caches. Pointer-address identity also risks accidental reuse after destruction/allocation at the same address.

**Remediation:** D13 assigns renderer state to the registration slot: an observed absent frame destroys it, while an unobserved remove/re-add is an update. A reset is only an update through an existing descriptor/source/access/custom authority that changes a registered structural fact; it adds no reset API/event/token. Key READY reuse by the plan's `content_key` and BUSY fallback by `structure_key`. A conservative callable clone without comparable semantics invalidates. D14 removes `Data_source::identity()`; no registry incarnation, `geometry_revision`, or series-instance token exists. The renderer's primary-content revision remains only a derived-artifact version.

**Verification:** a color/label-only update with stable explicit semantics causes no snapshot, range rescan, VBO upload, or custom-layer recreation; a conservative callable invalidates; callable or semantic revision invalidates once; observed removal destroys the slot and re-add starts cold; unobserved remove/re-add is an update; owner-control-block plus aliased-pointer cases never reuse across distinct source objects.

### P10 — Qt synchronization copies configuration and the full series map on unchanged frames

**Classification: Source-confirmed operation; cost is inferred.** `src/qt/plot_renderer.cpp:205-216` copies `Plot_config` and the complete map before consulting revision state.

**Remediation:** D11 makes widget API reads/mutations GUI-thread-only and uses Qt's blocked-GUI `synchronize()` as the sole render-input boundary. Compare `m_config_revision` before copying configuration and add a series revision incremented once by add/remove/clear/apply; copy the map only when it changes. After thread-contract enforcement/tests pass, remove redundant widget mutexes in Batch 3D rather than adding an event bus, model hierarchy, or second publication system.

**Verification:** with 1, 32, 256, and 1000 static series, record exactly one initial map/config copy and none until mutation. Measure allocations and synchronize p95; rendered and upload counters must be unchanged.

### P11 — Range/interpolation logic has reached the consolidation threshold

**Classification: Source-confirmed redundancy with a preserved exception.** Range logic exists in `Data_source::query_v_range()` (`src/core/types.cpp:851-954`), a private auto-range scanner (`src/core/auto_range_resolver.cpp:47-150`), and the Qt auto-adjust aggregator (`include/vnm_plot/core/algo.h:530-613`). However, `Data_access_policy::is_valid()` requires timestamps, while `tests/test_cache_invalidation.cpp:576-650` confirms that GLOBAL value-only access without timestamps falls back to a full snapshot range scan. Deleting the private scanner before the canonical producer learns that all-time case would remove adopted behavior.

**Remediation:** first make canonical `Data_source::query_v_range()` accept value-only or range-only access for an all-time GLOBAL query, scanning the whole selected snapshot without timestamp filtering. Time-bounded VISIBLE queries still require timestamps. Route auto-adjust through that producer and reuse `resolve_sample_at` for point semantics. Only after parity tests pass should `scan_series_range()` and its final unsupported branch be deleted. This preserves one producer without deleting the value-only oracle.

**Verification:** keep `test_global_value_only_access_falls_back_to_snapshot_scan` green and add range-only/all-time parity. Cover time order, interpolation, hold, gaps, nonfinite policies, and every source status. Only then may `rg "scan_series_range"` return no matches.

### P12 — The benchmark can report success without proving the real native path

**Classification: Source-confirmed measurement defect.** `benchmark/src/benchmark_window.cpp:538-605` can silently fall back to QRhi Null. Reports do not record the actual QRhi implementation/device, and `--finish` is optional/default false. CI builds the benchmark but does not run it, and benchmark helper tests are disabled by default. Current sample upload metrics omit line-window bytes.

The static Bars artifact at `C:\Users\imak\AppData\Local\Temp\vnm_plot_review_bench\inspector_benchmark_20260710_090221_SIM_Bars.txt` reports 2605 frames, two primary sample uploads per frame, 160 reported bytes per frame, and two monotonic scans. The live Trades artifact at `C:\Users\imak\AppData\Local\Temp\vnm_plot_review_bench\inspector_benchmark_20260710_090223_SIM_Trades.txt` reports 2665 frames, 5328 primary uploads, 39,239,776 reported primary bytes, and 130 monotonic scans. Treat these as artifact-backed diagnostic observations, not valid GPU-performance evidence: the reports omit actual QRhi implementation, the exact invocation was not retained, and line-window traffic is absent.

**Remediation:** hard-fail native-backend creation unless `--rhi-backend null` is explicitly requested. Record actual QRhi implementation, adapter/device, build type, text mode, dimensions, sample/LOD scenario, and finish state. Name submission-only and GPU-finished timings distinctly. Run fixed untimed warm-up frames, reset the profiler, and start the measurement clock only after static fill/thread startup; report cold startup separately. Add deterministic scenarios for 1/8/64 ordinary series and 2/4/8 stacked members with unrelated LODs and phase-shifted timestamps. Export scanned samples, snapshot count/time/bytes, primary/line/stack/uniform/known-custom upload bytes and total, composition time, allocations, producer wait, memory high-water, and output count. If custom-layer traffic cannot be observed, label the total incomplete.

**Verification:** a short fixed-seed native smoke in CI gates deterministic counters, shader creation, and non-null pixels. Raw timing gates run on pinned hardware with identical backend/finish/build parameters and retained artifacts. Diagnose the cold timeout with phase timing before setting a threshold.

### P13 — Documentation currently advertises behavior that the source does not provide

**Classification: Source-confirmed.** README lists `COLORMAP_AREA` at `README.md:150`, but the public enum has no colormap and API tests assert its absence. The VISIBLE LOD claim conflicts with the range implementation. The configuration example mutates `config.line_width_px` at `README.md:204` before `set_config`, while `set_config` intentionally preserves several widget-owned visual fields (`include/vnm_plot/qt/plot_widget.h:118-120`; `src/qt/plot_widget.cpp:219-231`). `benchmark/ARCHITECTURE.md:27-28` says the benchmark source copies on snapshot and computes ranges, while `benchmark/include/benchmark_data_source.h:21-64` exposes a held zero-copy view and relies on the base range scan.

**Remediation:** remove the nonexistent colormap bullet; implement the shared VISIBLE plan proposed in P1 rather than weakening its performance promise; update the example to call `set_line_width_px`; and regenerate benchmark architecture notes from the actual source contract. Longer term, leave each setting on one canonical public surface rather than duplicating it in config and widget setters.

**Verification:** compile README API snippets in a small smoke target, grep documented display styles against the enum, and review documentation in the same commit as any public contract change.

### P14 — Small cleanups should remain small

**Classification: Source-confirmed.** These are safe, bounded changes:

- replace the duplicate `floor_div` in `include/vnm_plot/core/algo.h:734` with the canonical, better edge-case-aware implementation in `time_units.h:433`;
- in RU-3A's D12_HOLD_BREAK, remove `cached_snapshot_hold` and all current per-series plan/cache/record hold storage after consume-scoped derivation; RU-3C2 later adds only cross-series/shared-key scheduling over those unchanged hold-free representations;
- reject/erase null descriptors in `apply_series_updates` rather than retaining a map key whose renderer resources survive because cleanup only sees key absence;
- make `Series_view_plan` own or directly expose the canonical `sample_window_t` instead of manually copying equivalent fields at `src/core/series_renderer.cpp:977-1004`;
- cache LOD scale topology only after declaring it immutable or adding `lod_layout_revision()`; otherwise the cache would be incorrect.

**Verification:** focused unit tests for floor division signed extremes, null replacement resource cleanup, and plan/window field parity. Add a topology revision invalidation test only if topology caching/revision is actually introduced. Do not create a shared helper for the two existing `checked_size_add/product` copies; two uses are below the governance N=3 consolidation threshold.

## First-class stacked-series contract

### Public API: extend ordinary series registration

**Derived from approved D3/D5.** Keep one canonical series registry and add one optional immutable group identifier:

```cpp
std::optional<int> stack_group_id;
```

Add `Basic_series_builder::stack(group_id)` and `clear_stack()`. An enabled series with no membership behaves exactly as today. Enabled members with the same group form one stack in ascending existing series-ID order. Existing source, access policy, interpolation, nonfinite behavior, color, label, preview source, and style remain per series. Reuse the existing ID as order instead of adding a second ordering concept; add a distinct order property only if an actual caller later needs to reorder layers without changing IDs.

This is preferred over a separate `Stack_data`/`add_stack()` object because it reuses the canonical registration/update/lifetime path and avoids a second descriptor hierarchy. `apply_series_updates` is the documented atomic way to change multiple members. Individual add/remove calls commit intermediate topology; callers requiring atomic group changes must batch. A member needs timestamp and scalar `get_value`; missing source/timestamp/scalar map to `FAILED(STACK_SOURCE_MISSING)`, `FAILED(STACK_REQUIRES_TIMESTAMP)`, and `FAILED(STACK_REQUIRES_SCALAR_VALUE)`. Silently dropping a failed member is forbidden.

An enabled NONE member still contributes mathematically but emits no own draw; `enabled=false` excludes it. Fewer than two enabled members is `FAILED(STACK_REQUIRES_TWO_MEMBERS)` and no visible built-in style is `FAILED(STACK_GROUP_HAS_NO_VISIBLE_STYLE)`. DOTS, LINE, and AREA combinations are supported.

For the first version, a stack member with `Qrhi_series_layer` is `FAILED(CUSTOM_LAYER_IN_STACK)`. The ordinary custom-layer ABI has no cumulative bottom/top contract; a future stacked ABI needs a separately approved use case/benchmark.

### Mathematical definition

**Owner-approved under D1/D2/D5-D9.** For ordered component functions `c1...cK`:

```text
S0(t) = 0
Sj(t) = Sj-1(t) + cj(t)
```

Layer `j` fills the band between `Sj-1` and `Sj`. Its boundary line is `Sj`; the last boundary is the requested `h`. Values are accumulated in `double`, checked, and narrowed to float only for GPU storage.

Pending P-R1 proposes that at each emitted composition position/member, one private
`checked_cumulative_step(series_id,base_y,component_y)` owns that arithmetic. It
requires finite inputs, performs the one ordinary binary64 addition used by the
executed stack plan, requires a finite result, and returns an immutable private
`{series_id,component_y,base_y,cumulative_y}` record. The private immutable
presented sequence retains the ordered records for every emitted position;
geometry/GPU conversion and VISIBLE range consume those exact records. Neither
reconstructs or adds fields again. Thus absorption such as `1e16+1` may produce
cumulative `1e16` while the record truthfully preserves component `1`.

- Negative values use algebraic stacking: a negative component descends from the preceding cumulative value. Do not silently use a separate positive/negative baseline because then the final top is not `f+g`.
- AREA draws the bottom/top band. LINE draws cumulative boundaries. DOTS draw cumulative knots. Zero is only the `S0` baseline.
- Auto-range includes every bottom and top, not only `h`. For `f=100, g=-100`, the visible range must still include the intermediate 100 band.
- The feature means “sum of the selected displayed LOD approximations.” The source API does not state whether a coarse LOD sample is a mean, extrema envelope, or exact raw sample, so claiming raw LOD-0 analytic truth would be false.

### Timestamp, interpolation, domain, and status semantics

**Derived from the plan register.** D4 and the register's labelled executable
refinements remain open. Each member is evaluated as its own selected piecewise
function:

- Candidate A builds the ordered union of selected member timestamps. Candidate B builds a view-derived shared grid. Neither may match array indices, require equal timestamps, or choose one operand's grid.
- LINEAR evaluates piecewise linearly at every emitted position. STEP_AFTER is right-continuous: a value becomes active at its timestamp and holds until the next. Candidate A emits a left-limit event and then a right-value event at a changing step timestamp; without both, geometry would bridge the discontinuity. Candidate B evaluates right-continuously at grid positions and accepts the measured/signed-off screen-space displacement described by its approximation contract. Mixed modes are allowed.
- Treat both snapshot segments as one logical `at(i)` sequence. Equal timestamps collapse first to the greatest logical `i`, regardless of traversal direction; nonfinite processing then applies to that winner without resurrecting an earlier duplicate.
- ASCENDING and DESCENDING use direction-aware cursors. UNKNOWN classifies within one acquired snapshot; proposed RU-3C2 reuse requires P-D6's weak-owner/alias identity, stable nonzero observed sequence, and access semantics. Truly UNORDERED data is `FAILED(UNORDERED_STACK_INPUT)`; sorting every frame is expensive and semantically ambiguous.
- LINEAR consecutive finite knots define closed spans. STEP_AFTER is right-continuous on the same closed domain and retains left/right event order at a jump. Touching spans merge only when the function is defined at their shared endpoint; `BREAK_SEGMENT` removes the knot and incident spans, `SKIP` reconnects surviving neighbours, `REPLACE_WITH_ZERO` substitutes finite zero, and `REJECT_WINDOW` is `FAILED(NONFINITE_REJECTED)`.
- Under pending P-D2, normalize every member before intersection. REJECT failure precedes would-be EMPTY. Both LINEAR and STEP_AFTER HOLD_LAST_FORWARD extend the latest defined value constantly through the closed right endpoint, never left/cross-break; DRAW_NOTHING does not. Candidate A must retain every intersection endpoint and selected breakpoint within `R_A=K*B`; Candidate B reserves endpoints then optional grid positions within `R_B=N`; excess is `FAILED(FRAGMENTATION_BUDGET)`.
- P-D2 counts semantic selected timestamp positions separately from physical
  visits: retained timestamps, required brackets, and a synthetic hold endpoint
  consume the existing B/N capacity, never an appended `+1`; `V_observed`
  separately counts every physical inspection across all attempted/selected
  LODs, and no physical inspection is a selected-input position.
- P-D15 enforces visits, output pairs, and upload writes prospectively before
  the next inspection/create/write. The limit+1 operation has no side effect and
  counters never exceed their per-unit or admitted-frame allowances.
- Pending P-D6's `structure_key` gates BUSY fallback. Its `content_key` is the
  register's compact equality LOD/sequence/logical-window/hold/origin/version
  equality key. Inherited source identity is a non-owning weak owner identity
  plus alias pointer; equality requires neither `owner_less(a,b)` nor
  `owner_less(b,a)`, then equal alias pointers—never control-block address serialization/hash or strong
  lifetime. The key excludes both physical snapshot segmentation and
  normalized/drawable spans, values, arrays, and resources. READY reuse
  additionally requires stable nonzero sequences/D10; a zero-key may identify
  retained STALE_BUSY content. Both keys and their weak-owner/alias identity are
  renderer-private; public results expose neither key nor source identity.
- Pending P-R1 requires pending P-C1 to be ratified before or in the same owner
  disposition and both to be recorded by review-clean consolidated A1; P-R1
  cannot be approved or implemented first. It leaves the ordinary approved D9
  result unconditional and, under A1-recorded ratified P-C1, gives only each
  `COMPLETE` stack result table's READY/STALE_BUSY entries an optional presented
  `content_frame_id` plus `content_cursor_state_id`. Under A1-recorded ratified
  P-C1, every executed plan and enclosing D9 result also carries the current-attempt
  monotonic `cursor_state_id`; activation, movement/request replacement, and
  deactivation each increment it. Every group/view record carries value-only
  `stack_group_id` and `Stack_view_kind::MAIN`/
  `Stack_view_kind::PREVIEW`; every member carries `series_id`, including those
  in a group/view with `FAILED(reason)` disposition and those with
  `NOT_EVALUATED` origin. Per-series records have no failure disposition. MAIN
  records precede PREVIEW, groups order by lowest enabled series ID, and members
  by series ID. These labels self-describe delayed results but never authorize
  reuse. The enclosing
  D9 result retains `publication_frame_id` and executed-plan config identity,
  which rejects stale consumption after configuration/topology change.
  Per-series origin is exactly structural-label-only NOT_EVALUATED,
  terminal-source-outcome-only CURRENT_OBSERVATION, or identity-tagged
  PRESENTED_CONTENT.
  STALE_BUSY keeps current outer publication, reuses private retained geometry,
  and copies only the retained value record, including the complete optional
  indicator batch and old `content_cursor_state_id`, into the independent public
  stack table even when no batch exists; current attempts remain trace/counters
  only. BUSY and EMPTY
  acquisitions continue so a later first FAILED can stop and outrank;
  otherwise EMPTY outranks BUSY, and no-retained
  BUSY is `FAILED(SOURCE_BUSY)` with each required member's terminal
  CURRENT_OBSERVATION.
  READY replaces retention; EMPTY, every failure/budget/allocation outcome, and
  structural change remove eligibility before publication. Only uninterrupted
  BUSY may continue stale.
  PRESENTED_CONTENT is constructed only for final READY or copied only for final
  STALE_BUSY. Final EMPTY/every failure projects every acquired source to its
  terminal CURRENT_OBSERVATION even if READY selection/window derivation had
  succeeded before a later intersection/nonfinite/fragmentation/numeric/output/
  resident/RHI failure; the optional derived window remains, but content ID,
  value, range, and indicator do not.
- The stack section/table/entry/member types have private storage, no public
  mutation or unrestricted construction, and frame-producer-only named tagged
  factories. COMPLETE alone owns a table; result-storage failures own none.
  READY/STALE_BUSY require content frame/cursor IDs, range, and an exact optional
  `stack_indicator_batch_t`; EMPTY and every failure expose no content ID/range/
  batch.
  NOT_EVALUATED contains only `series_id`.
  `observed_ready` is created only after the controlling READY attempt/consume
  scope terminates and its hold is released/destroyed. It has terminal attempted
  LOD, observed sequence, and a window only if derived; subsequent order/visit/
  window/LOD-budget/other failure does not erase it or fabricate value/content.
  `observed_empty` has controlling terminal attempted LOD/exact sequence
  including zero/no window; `observed_busy`/`observed_failed` have controlling
  terminal attempted LOD/zero sequence/no window. The one public source outcome
  is final READY PRESENTED_CONTENT, retained final STALE PRESENTED_CONTENT, or
  one terminal CURRENT_OBSERVATION.
  Earlier LOD attempts and all visit counters remain internal diagnostic trace/
  profiler records tagged by group/view/series/attempt. `UNSUPPORTED` is
  not a completed acquisition. PRESENTED_CONTENT separately carries selected
  LOD/window only for final READY/STALE. Mismatched observers return absent/null.
- Public `Stack_failure_reason` contains exactly the register's 19 named stack
  reasons. `failed(reason,observations)` accepts only it. Result-storage budget
  and allocation failures remain enclosing section dispositions and are never
  entry enum members.
- Public `Stack_indicator_status` is exactly READY/EMPTY. A request applies to
  all `K>=2` enabled members, including `NONE`, and produces exactly `K`
  ascending-series items; there is no filter/subset or zero-length present
  batch. Cursor evaluation never inserts a position, changes geometry/budgets/
  admission/acquisition, or queries raw/pre-grid values. It samples only the
  frozen private presented sequence: Candidate A uses its bounded normalized/
  event-union interpolation/duplicate/nonfinite/gap/hold rules; Candidate B uses
  the finalized bounded grid, exact grid interpolation, and displaced STEP
  transitions, never the pre-grid curve. Stored component/base/cumulative fields
  are interpolated independently with no cumulative add or reconstruction; an
  exact emitted position may reuse its record. Outside the common domain or in a
  gap the whole group is EMPTY. The private READY factory accepts only those
  cursor-sampled records, verifies canonical first `+0.0` and bitwise adjacency,
  copies their fields, and uses the final cumulative as total. Public callers
  cannot create or supply records or fields. EMPTY contains only each
  `series_id` and no total value; mixed or inconsistent status/value is
  unconstructible. Absence alone means no executed/retained request. A present
  batch request ID equals presented `content_cursor_state_id`. STALE_BUSY copies
  the whole old batch/ID; EMPTY/group failure has none.
- An allocation-free metadata prepass first maps every deterministic MAIN-then-
  PREVIEW group/member position, registry failure, and fixed admission slice/
  disposition. It allocates no map container or unit resource. After the map is
  complete, one independently accounted frame-wide table allocation covers
  every accepted/rejected position before acquisition; rejected units still
  have no source or unit staging/geometry/QRhi/retained allocation.
- P-R1/P-D15 add a fixed allocation-free `stack_result_section_t` envelope to
  the ordinary result. Its exact dispositions are `COMPLETE`,
  `FAILED(RESULT_STORAGE_BUDGET)`, and
  `FAILED(RESULT_STORAGE_ALLOCATION_FAILED)`. Either stack storage failure has
  no stack table, performs no stack acquisition/geometry, and clears every
  stack stale entry while ordinary pixels, statuses, counters, planning,
  rendering, and results continue unchanged. Stack entry invariants begin only
  after the exact independent table exists under `COMPLETE`.
- Accumulation remains double. Pending P-R1 requires a finite double after each
  addition and a finite v1 float after conversion; ordinary finite rounding is
  accepted and exact float roundtrip is not required.
- Snapshots from different sources are individually stable, not transactionally simultaneous. Different LOD snapshots of the same source have no stronger atomicity guarantee. The contract must say this. Applications needing atomic cross-signal or cross-LOD publication must publish through one coordinated snapshot/revision contract.

### Bounded independent LOD planning and unresolved sampling strategy

**Approved common constraints; D4 strategy unresolved.** Neither “merge all LOD 0 samples” nor “give every member a full one-sample-per-pixel input before an unbounded union” is acceptable. The first grows with history; the second can emit approximately `K^2*width` cumulative band samples.

Pending P-D15 in the sole register owns the exact `W/K/C/D/M`, visit,
candidate-independent uniform-slot, upload-reservation, frame-admission, and
resident-resource formulas. They are intentionally not duplicated here.

Extend the planner request with separate `render_width_px` and `max_visible_samples`; do not overload shader/view width. Every strategy selects each source independently and includes brackets/holds. If a coarsest complete window exceeds the per-input cap, return `FAILED(LOD_BUDGET_EXCEEDED)` rather than dropping input or using raw history.

**Pending P-D2 candidates.** The sole register owns the exact Candidate A/B
input/output capacities, minimums, endpoint/breakpoint obligations, hold-slot
accounting, and typed failures. Architecturally, A is an exact union of the
bounded selected curves, while B is a deterministic bounded shared grid whose
spike/step approximation remains part of D4 evidence. This review does not
duplicate the operative formulas.

Do not choose a default from asymptotic argument alone. Prototype both outside the public API with identical independent LOD selection and hard `M`, `V_limit`, and `H_limit` counters plus identical D15 frame admission. Compare native-backend compose time, `V_observed`, total bytes, allocations, producer wait, and full-frame p50/p95/p99 for K={2,8,32} and W={800,3840}; retain hashed artifacts for phase-shifted narrow spikes, mixed LINEAR/STEP_AFTER discontinuities, gaps, and cancellation. The evidence unit records no human verdict; the owner reviews that exact evidence identity, records the comparative visual verdict, and selects one strategy before its tests become product oracle. Do not ship two speculative modes.

For either candidate, final construction must satisfy the A1-recorded ratified
P-D15 checked accounting exactly; acquisition/copy/alignment remain separately
measured.

`C` should remain internal until native-backend quality/performance results show that callers need control. Verify the hard output/byte bounds and explicit budget failures for both prototypes.

Do not add LOD hysteresis as part of this feature. Commit `e605b5f` deliberately reverted hysteresis; absent a new owner decision and evidence, selection must remain deterministic under the current policy.

### Shared frame plan and cache

**Derived from D9/D12.** The internal production frame and stack producers follow this dependency flow:

```text
immutable series registry
        |
consume-scoped acquisition scheduler: source object -> at most one live hold
        |
snapshot-free per-series plans ----> ordinary VISIBLE range and renderer
        |
stack composition plans ----------> stacked range and stacked renderer
        |
executed-plan disposition ---------> ordinary D9 immutable frame result
                                      + Stage 5 stack-result extension
```

RU-3C1 produces the ordinary D9 result without a P-R1/P-D15 prerequisite.
Stage 5 later appends the stack section; stack result-storage failure does not
alter the ordinary section.

D13 makes each renderer registration slot the lifecycle owner. Pending P-D6's
compact `structure_key` contains only non-content request facts. Its compact
equality `content_key` uses the register's scalar LOD/sequence/logical-window/hold-
endpoint/origin/version tuple—never physical snapshot segmentation, normalized
or drawable spans, payload values, arrays, or resources. Inherited source
identity is weak/non-owning plus alias pointer. D14 removes
`Data_source::identity()`; no incarnation/revision/reset token exists.

READY composite reuse requires identical `content_key`, stable nonzero sequences, and non-conservative semantics. Otherwise correctness wins: recompute the bounded group and record why. BUSY compares only `structure_key`, retains the previous complete `content_key` wholesale, and never consumes a partially fresh operand set.

Those keys remain private renderer/cache facts. Public immutable results neither
contain nor accept keys, weak owners, or alias pointers. Their group/view/member
IDs are self-description labels without reuse authority; public content
provenance is only content frame ID and exact per-series presented sequences.

Pending P-D15 replaces unspecified geometric vector growth only for capped
stack-exclusive storage with a narrow exact-size owning contiguous array plus
explicit count; it introduces no general allocator/container framework and
leaves ordinary Stage 3 vectors unchanged. This makes live/temporary cap
accounting truthful; unchanged steady state still reuses exact storage and
allocates zero heap blocks. Do
not implement suffix-only incremental recomposition from `current_sequence()`:
the current contract does not promise append-only immutable prefixes or report
the first changed index. If profiling later proves this necessary, first add an
explicit source change-range contract, then key and test it.

### GPU representation and draw order

**Owner-approved v1 contract.** Use one group VBO containing contiguous
per-member interleaved `{float t_rel, float bottom, float top}` blocks and
`static_assert(sizeof(record)==12)`. Both D4 candidates use this exact record;
there is no v1 split-time alternative or layout experiment.

The layout uses group-shared span metadata and a separately keyed padded
`{float t_rel,float top}` LINE boundary buffer with four prev/p0/p1/next
bindings. Pending P-D15 in the sole register owns all exact byte, uniform-slot,
alignment, and cap arithmetic. If formats or ownership change, amend and
ratify that register text and its tests before implementation.

Prepare/upload outside the render pass and bind/draw inside. Draw filled bands
in component order, then cumulative boundaries so the final line represents
`h`. Counters separately report base, boundary, metadata, uniform, total bytes,
upload count, and vertices. A split shared-time/ordinate layout is deferred
unless the v1 record misses an accepted bytes/p95 target and a later measured
plan amendment approves it.

Stacking does not use `Qrhi_series_layer`: that API describes one ordinary
planned window and has no cumulative bottom/top contract. Batch 3A narrows the
ordinary custom-layer ABI to a call-scoped non-owning prepare view with no hold
and removes raw snapshot access from record; a future stacked custom-layer ABI
requires a separate owner-approved use case.

### Frame-wide and resident resource ownership

Pending P-D15 proposes deterministic metadata-only MAIN-before-PREVIEW frame
admission and hard instantaneous live caps for explicitly scoped stack-only CPU
storage and exact requested stack QRhi buffers. Exact-capacity internal CPU
storage makes allocator-requested bytes truthful; ordinary per-series arrays
are excluded. `renderer_owned_requested_live_bytes` is the sole CPU-cap oracle:
successful renderer-owned counted allocation increments it, and atomic transfer
out or renderer-owned destruction decrements it once.
`allocation_requested_live_bytes` is separate lifetime evidence: successful
allocation increments it, publication/copy/non-final destruction leave it
unchanged, and final deallocation decrements it once. Failed allocation changes
neither. Allocator/OS rounding, RSS, virtual size, and physical/OS-committed
memory participate in neither; separate process-memory evidence records those
physical effects. Admission assigns every accepted unit an immutable
`{M,V_limit,H_limit}` slice. Frame totals are used only for slice admission;
runtime checks consume only that unit's slice and never share balances, transfer
slack, refund capacity, or readmit a rejected unit.

Renderer-private retained/staging backing, builders, temporaries, and current/
stale entries never cross renderer ownership or expose an external owning
handle. Internal aliases remain renderer-owned and change neither counter. Each
private successful allocation increments both exact counters, and only its
actual deallocation decrements both exactly once. A private
`renderer_retained_stack_entry_t` owns P-D6 keys, retained geometry/resources,
and one value-only retained presentation record. The public
`stack_result_entry_t` contains only `stack_group_id`, `Stack_view_kind`,
disposition, per-series `series_id`/origin/status/sequence and origin-tagged
terminal-attempted-or-selected LOD/window, rendered range, optional indicator
batch, `content_frame_id`, and READY/STALE `content_cursor_state_id`. Under
A1-recorded ratified P-C1, the enclosing D9 result also carries current-attempt
`cursor_state_id`. The stack result contains no geometry, spans, samples,
resources, source identities, visit counters, or cache keys.

One repo-local table-specific counted backing primitive performs one allocator
request for an atomic thread-safe refcount/immutable layout header, checked
padding, and all metadata/payload regions. Checked arithmetic supplies every
size/alignment/offset and the exact requested header/padding/payload bytes;
opaque allocator overhead is excluded. Prospective cap excess is result-storage
budget failure, while admitted allocator failure is result-storage allocation
failure. Metadata/payload freeze before publication and only refcount remains
mutable; no `shared_ptr`, opaque control block, or guessed control size is used.

`stack_result_table_t` is a copyable immutable handle to that public-only block.
Copy/move/destruct touch only the pointer/refcount and allocate nothing;
cross-thread non-final destruction preserves stable offset-derived views, while
publication atomically decrements only
`renderer_owned_requested_live_bytes`, copies and non-final destruction change
neither counter, and final deallocation decrements only
`allocation_requested_live_bytes` once. A renderer-private allocation increments
both and only actual deallocation decrements both; failed allocations and
internal aliases change neither, and no path double-debits. The public table is
the intentionally transferred allocation: transfer decrements only the renderer
counter and final deallocation only the lifetime counter. Any future intentional
transfer must use that explicit contract and cannot export private backing by
reclassifying an alias. No raw group/member pointer, rebasing, or private
renderer reference is stored.
Ordinary D9 result storage and opaque allocator overhead remain outside the
stack cap. The private retained target's replacement transaction either
preserves the old target while a
complete replacement fits or debits it before retrying, but every resident/
allocation/frame/output budget failure ultimately removes target eligibility
and stale fallback. No partial set is installed and no opaque pipeline/SRB/texture
residency is claimed. The sole register owns the exact formulas, counted scopes,
order, statuses, and transaction algorithm. Pending P-R1 owns the corresponding
immutable result presentation.

### Auto-range, preview, and indicators

**Derived Stage 5 behavior.** The shared stack plan computes min/max for every emitted bottom/top during its one composition pass. VISIBLE range consumes exactly those extrema.

GLOBAL and GLOBAL_LOD should not construct full-history composite geometry. Extend `data_query_context_t` with an explicit value projection: existing ordinary rendering may request draw extent, while stacks request `SCALAR_VALUE`, which reads `get_value` even when `get_range` exists; an override that cannot honor the projection returns UNSUPPORTED and uses the canonical scalar fallback. GLOBAL uses each source's LOD 0; GLOBAL_LOD uses each source's own coarsest level, without relating ladder indices. First compute every selected level's effective time domain, including explicit right hold, and intersect them; a disjoint intersection returns EMPTY. Restrict each scalar range query to that common domain, then compute conservative prefix bounds by summing minima/maxima for every prefix plus zero. This cannot clip a defined band, though it may remain wider because extrema and internal valid spans need not overlap simultaneously; exact internal-gap composition is intentionally avoided for global modes. Cache domain and scalar results by source/range semantic keys.

Preview invokes the same planner independently with effective preview sources, LODs, width, and range; it must not fall back to unrelated zero-based areas.

At the cursor, evaluate the published rendered representation for every enabled
member, including `NONE`, by sampling the frozen private presented record
sequence; never query source data, add a cursor position, or alter the executed
plan. Candidate A applies its exact frozen event-union rules. Candidate B applies
exact interpolation to its finalized grid, including displaced STEP transitions,
not a fresher/pre-grid curve. Independently interpolate stored component/base/
cumulative fields without another cumulative add or reconstruction; exact
emitted positions may reuse records. The READY factory copies only these private
cursor-sampled records after adjacency checks, or emits one all-EMPTY batch for
the complete gap/out-of-domain group. Place every marker at `cumulative_y`.

Under pending P-C1, GUI consumption first validates config identity. It then
reads current cursor activation independently of result payload: inactive clears/hides every stack
tooltip/marker/total immediately on deactivation, and result consumption keeps
it clear while ignoring delayed/retained content. If
active, READY/STALE_BUSY compare their retained `content_cursor_state_id`, while
EMPTY/FAILED/section failure compare the enclosing current-attempt
`cursor_state_id`; mismatch performs no mutation. On a match, READY-status data
replaces the whole group, EMPTY-status/no-batch/group EMPTY/FAILED clear/hide the
group, and section failure clears all groups. A present batch request ID equals
its presented cursor-state ID. Activation, movement/replacement, and deactivation
all advance the monotonic state, so old absence/failure cannot clear a newer
request and READY→deactivate→BUSY stale cannot make content reappear. Never query
raw LOD 0 or bypass the selected representation.

## Designs reviewed: rejected and unresolved

| Design | Decision and concrete replacement |
|---|---|
| Require equal timestamps or match array indices | Rejected: loses or mispairs samples. Use either the explicit event union or the explicit shared grid selected by the A/B gate. |
| Use one component's timestamps as master | Rejected: operand-order-dependent and can miss another component's breakpoint. A shared grid, if selected, is view-derived rather than borrowed from an operand. |
| Require equal LOD indices | Rejected: indices and scales are source-local. Apply an independent visible-sample budget to each source. |
| Always compose LOD 0 | Rejected: work grows with history and ignores the library's LOD contract. Bound selected input/output by framebuffer width. |
| Give every component the full pixel budget | Rejected: cumulative output can grow as `K^2*W`. Partition the single M budget according to the chosen candidate. |
| Unbounded exact union | Rejected: mathematically attractive but violates the performance requirement. Candidate A is exact only for its budgeted selected curves. |
| Bounded shared-grid resampling | **Unresolved candidate B:** it respects M and retains more per-input density for large K, but adds spike/step approximation. Choose against Candidate A only through the named native-backend/visual A/B gate. |
| Precomputed full-history `Summed_series_source` | Rejected as the first-class visual feature: `try_snapshot(lod)` has no view budget, unrelated LOD Cartesian combinations are undefined, and updates can rebuild/copy history. It remains an application option when analytic derived data is independently required. |
| Custom QRhi series layer | Rejected: wrong ownership/planning boundary. Implement one shared core stack plan consumed by range and RHI. |
| GPU compute merge first | Rejected: current inputs are CPU snapshots and backend portability matters. Start with bounded linear CPU composition; revisit only after profiling identifies composition as dominant. |
| Separate positive/negative baselines | Rejected for this request because the top would no longer necessarily be `h=f+g`. Add only as a separately named future contract. |
| Suffix incremental cache inferred from sequence | Rejected: sequence does not prove append-only immutable history. Add an explicit change-range contract first if measurements require it. |
| Separate `add_stack()` descriptor hierarchy | Rejected for the simpler caller-disciplined contract because it duplicates registration, preview, style, and lifetime APIs. Reconsider a group-owned immutable descriptor only if the owner requires group-topology atomicity even when callers use individual add/remove operations. |
| Reintroduce LOD hysteresis | Rejected under current repository policy; a prior commit explicitly removed it. Requires a new owner decision and evidence. |

## Governed implementation batches

Each batch must be independently useful, reviewed, and measured. Do not land unused scaffolding or compatibility paths.

### Batch 0 — Historical dependency and CI integrity closure

Files/tools: `vnm_plot/CMakeLists.txt` and package smoke configuration; separately authorized sibling `vnm_msdf_text/.github/workflows/ci-windows.yml`; `actionlint` (or equivalent GitHub Actions-aware validation) plus an actual GitHub workflow rerun.

Historical result: required dependency contract is present on `master`, all
eight dependent `vnm_plot` jobs passed, and dependency Linux/Windows/macOS/
FreeBSD passed at `vnm_msdf_text@b9c216a`. Batch 0 is closed; resolved dependency
commit recording and no-sibling install/consumer smoke remain standing gates,
not open Batch 0 work.

Historical gate: workflow validation and real Windows jobs passed at `b9c216a`;
the exact fetched revision was recorded; dependency and dependent four-platform
matrices passed.

### Batch 0A — Historical style debt closure

Files: `src/core/series_renderer.cpp:95`, `src/core/types.cpp:47-53,389-394`, and `tests/test_msdf_lcd_shader_reference.cpp:217-225`.

Historical deliverable: the formatting-only baseline and necessary
non-behavioral style-gate repairs closed as plan Batches 1B/1C at
`vnm_plot@2ec8013`; their failure provenance remains in the ledger.

Historical gate: the governed pipeline, initialized Release build, CTest 21/21,
and hosted matrix passed at `2ec8013`. The shadowed time constants remain a
separate Batch 3B source action because their removal changes source tokens.

### Batch 1 — Truthful hot-path observations and unchanged-frame reuse

Files: `include/vnm_plot/rhi/series_renderer.h`, `src/core/series_renderer.cpp`, QRhi lifecycle tests, benchmark profiler/reporting, `benchmark/ARCHITECTURE.md`, benchmark CMake/workflows.

Deliverable: test hooks absent from production, complete primary/line upload counters, keyed unchanged-frame buffer reuse, actual backend metadata, deterministic CI smoke.

Gate: static second frame has zero library restage/upload; a LINE mutation uploads primary content once and derived line-window content once; native backend and total bytes recorded; non-stack pixels/draws unchanged. Under identical Release build, backend, finish mode, dimensions, sources, seed, and instrumentation, compare before/after full-frame p50/p95/p99, GPU-finished time, allocations, producer wait, and memory for static and live workloads. Accept only with the deterministic counter win and no statistically meaningful end-to-end regression.

### Batch 2 — Canonical ownership, status, evaluator, and frame plan

Files: `include/vnm_plot/core/types.h`, `basic_series_builder.h`, `access_policy.h`, `algo.h`, `time_units.h`, `src/core/types.cpp`, `series_window_planner.*`, new `frame_series_planner.*`, auto/frame range, `include/vnm_plot/rhi/series_renderer.h`, `src/core/series_renderer.cpp`, `plot_renderer.cpp`, `plot_widget.cpp`, `examples/hello_plot`, `examples/function_plotter`, `benchmark/include/benchmark_data_source.h`, `benchmark/include/benchmark_window.h`, `benchmark/src/benchmark_window.cpp`, `README.md`, and focused tests.

Deliverable: after owner-ratified P-S1 and review-clean consolidated A1, RU-3A
supplies shared ownership, top-level snapshot-result sequence, exact D12 public
method/domain contract, structural custom borrowing, and atomic removal of every
current per-series plan/cache/record hold. RU-3B, only after owner-ratified
P-D7/P-Q1 and review-clean consolidated A1, supplies checked floating ingestion,
integral-only members, and canonical `query_sample`/range semantics. RU-3C2
solely replaces independent hold-free acquisitions with cross-series/shared-key
scheduling; it does not repeat hold removal. RU-3C1 supplies snapshot-free frame
truth and VISIBLE/render/D9 identity, but its atomic cursor/result cluster waits
for owner-ratified P-C1 recorded in review-clean consolidated A1. RU-3C2's P-D6
reuse work likewise waits for owner-ratified P-D6 in that A1. No stack-only
public metadata or unused planner lands in this stage.

Gate: RU-3A proves snapshot READY/EMPTY/BUSY/FAILED top-level sequence semantics, D12 conformance, writer-before-record, and no surviving current hold. RU-3B separately proves proposed cursor and range gates plus D7 trait/rational boundaries. RU-3C2 proves exactly one shared scheduler and one acquisition per shared key. RU-3C1 proves frame/range/render/result parity. Each RU retains its own ASan/TSan/UBSan, warning-clean, and no-regression evidence; one later gate never retroactively makes an earlier hash truthful.

### Batch 3 — Atomic end-to-end stack feature

Files: public series metadata/builder, new `src/core/stack_planner.{h,cpp}` (with internal placement consistent with the layout target), scalar range projection, frame-range integration, Qt registry/API/preview/indicator publication, series renderer state/commands, one stack-band shader and boundary path, CMake/QSB assets, focused math/RHI/interaction tests, example/QML, benchmark scenarios, and architecture documentation.

Deliverable: one usable public capability sequenced through the plan's distinct
`RU-5-production`, `RU-5-public`, and `RU-5-final-evidence` containers—A/B-selected
bounded composition strategy, independent sequential LOD observations,
cumulative bands, typed status/domain handling, D15 frame/resident budgets,
VISIBLE/global scalar range, cached RHI rendering, preview parity, plan-derived
frame-result indicators, example, and representative benchmark. Production
actions are internal but immediately consumed by the benchmark/internal entry.
Public metadata/registry reachability, Qt/result/preview, normative API/docs,
and package-consumer smoke activate atomically as `STACK_PUBLIC_ACTIVATION`, so
no unused, unreachable, or undocumented public intermediate exists. A later
independent example/tutorial unit is non-contractual. The three containers are
not combined into one review unit.

Gate: complete the then-ratified acceptance matrix after D4 selection; freeze Candidate B's finalized-grid cursor interpolation and displaced STEP-transition evaluator before evidence; hard band-ordinate and total-byte bounds; linear visit counters; zero steady allocation after warm-up; stable/non-conservative unchanged second frame zero composition/upload; unstable/conservative inputs perform bounded recomposition and expose the disable reason; shader bake and prepare/record lifecycle on every backend; non-stack counters bit-for-bit unchanged. Final-evidence A2 first generates the exact performance/visual manifest and receives technical review. Only afterward may a separate nondelegated human approval be bound to A2's exact source/manifest identity; A3 validates and retains that binding before any golden image becomes an oracle. Workers/reviewers cannot create or substitute approval. Compare full-frame native-backend p50/p95/p99, CPU plan/compose, GPU-finished time, total bytes, draws, allocations, snapshot bytes/time, alignment scans, producer wait, memory, and LOD changes. Require no statistically meaningful non-stack regression under identical builds; agree any numeric timing threshold from baseline evidence rather than inventing one.

## Stack acceptance matrix

This matrix is non-normative evidence derived from the sole decision register in
`VNM_PLOT_IMPLEMENTATION_PLAN.md`. If wording diverges, the register wins and
this table must be regenerated; it never creates a second contract. D4 remains
evidence-gated, and labelled executable refinements remain owner-pending.

| Case | Required observable result |
|---|---|
| Offset LINEAR timestamp grids | Candidate A includes both breakpoint sets; Candidate B evaluates both curves at every shared grid point. In either prototype, the final boundary is the double-precision sum at every emitted position. |
| Sampling strategy A/B | Pending P-D15 gives both candidates identical fixed reservations/totals and workloads; the evidence unit compares without selecting, then the owner chooses D4. |
| Mixed LINEAR and STEP_AFTER | Candidate A emits left/right at the true selected-curve timestamp; Candidate B emits left/right at its sampled grid transition. Both avoid a diagonal bridge, and indicators match the chosen representation; Candidate B's displacement is part of visual A/B approval. |
| Unrelated LOD counts/scales | Each source records an independently selected level satisfying its budget; no same-index assumption appears. |
| Coarsest LOD exceeds budget | Group reports `FAILED(LOD_BUDGET_EXCEEDED)`; there is no LOD-0 fallback or partial render. |
| Strategy budget too small | Candidate A B<2 or Candidate B N<2 reports `FAILED(STACK_BUDGET_TOO_SMALL)`; neither constructs ambiguous zero/one-point geometry. |
| Descending and UNKNOWN-monotonic | Direction-aware result matches ascending reference; stable nonzero sequence caches classification, while zero/unstable sequence scans once during its consume-scoped acquisition only. |
| Truly unordered | `FAILED(UNORDERED_STACK_INPUT)`; no sort allocation or partial sum. |
| Duplicate timestamps | Greatest logical `data_snapshot_t::at(i)` index wins consistently in ascending/descending planner and indicator before nonfinite processing. |
| Equidistant NEAREST | Pending P-Q1 chooses the greater timestamp and its D8 winner by exact unsigned distance, independent of order/sign. |
| Negative/cancellation `100 + -100` | Second band descends to zero; range still includes the first cumulative 100. |
| Disjoint/partially overlapping domains | Only intersection renders, except explicit right hold; no left extrapolation. GLOBAL/GLOBAL_LOD intersect selected-level domains first and return EMPTY when disjoint. |
| Nonfinite policies | BREAK/SKIP/ZERO/REJECT outcomes match the canonical evaluator and affect the whole sum where undefined. |
| BUSY/FAILED/EMPTY | BUSY/EMPTY continue in ascending acquisition order so a later first FAILED can stop and outrank; otherwise EMPTY outranks BUSY. STALE_BUSY keeps current `publication_frame_id`, reuses private retained geometry, and copies only the retained value record—including old `content_cursor_state_id` even without a batch—into the independent public table; current attempts remain trace/counters. Without retained content, BUSY is `FAILED(SOURCE_BUSY)` with all required entries CURRENT_OBSERVATION. READY replaces retention; EMPTY/every failure/budget/allocation/structural change clears it, so only consecutive BUSY can stale. |
| Multiple failures | Pending P-R1 selects one canonical disposition by phase/series/reason order. Every result has publication identity; only READY/STALE_BUSY have presented identity. A pre-acquisition stop is all NOT_EVALUATED; each completed member through the first source FAILED has one terminal CURRENT_OBSERVATION and only later series are NOT_EVALUATED. |
| Result self-description/order | Every group/view record contains `stack_group_id` and MAIN/PREVIEW kind; every member contains `series_id`, including members of a `FAILED(reason)` group/view and members with `NOT_EVALUATED` origin. Per-series records have no failure disposition. All MAIN groups ordered by lowest enabled series ID precede equivalently ordered PREVIEW groups; members ascend by series ID. Delayed/topology-changed results remain self-describing, while enclosing executed-plan config identity rejects stale consumption and labels never authorize reuse. Under pending P-C1, enclosing current-attempt `cursor_state_id` and presented `content_cursor_state_id` remain distinct. |
| Public result privacy | `stack_result_entry_t` contains only structural IDs/view kind, disposition, per-series origin/status/sequence, origin-tagged terminal-attempted-or-selected LOD/window, rendered range, optional indicator batch, content frame ID, and READY/STALE content cursor-state ID. No private cumulative-step record, visit counter/earlier attempt, geometry, span, sample, resource, source identity, weak owner, alias pointer, `structure_key`, or `content_key` is exposed, serialized, copied, or accepted by the public result/API. Private `renderer_retained_stack_entry_t` owns keys, retained geometry/resources, and one value-only retained presentation record. |
| Source-observation variants | One public controlling source outcome is final READY PRESENTED_CONTENT, retained final STALE PRESENTED_CONTENT, or a terminal READY/EMPTY/BUSY/FAILED CURRENT_OBSERVATION. `observed_ready` is created after its terminal attempt/consume scope terminates and its hold is released/destroyed; later visit/order/LOD-budget/window/other failure does not erase its terminal LOD/sequence/derived optional window or fabricate value/content. Earlier LOD attempts and all visit counters stay in complete internal group/view/series/attempt-tagged trace; prospective-limit/writer-progress tests prove no scan past the limit and no trace/public conflation. `UNSUPPORTED` cannot construct CURRENT_OBSERVATION. |
| Result structural states | Section/table/entry/member storage is private and frame-producer tagged factories are the only construction path. COMPLETE alone has a table. READY/STALE_BUSY require content frame/cursor IDs, range, and the plan's exact optional indicator batch. EMPTY and every failure expose no content ID/value/range/batch; every acquired source projects one terminal CURRENT_OBSERVATION even if selection/window derivation succeeded before a later group failure. NOT_EVALUATED has only `series_id`. Matched const observers expose payload and mismatched observers are absent/null. |
| Failure enum boundary | Public `Stack_failure_reason` contains exactly the register's 19 stack reasons and is the only entry failure-reason type. Result-storage budget/allocation failures are enclosing section dispositions only and cannot appear in the enum or an entry. |
| Indicator batch | Batch absent alone means no executed/retained request. A request covers every enabled member including `NONE` and produces exactly `K>=2` ascending items. Private READY accepts only ordered cursor-sampled records from the frozen A event union or finalized B grid, verifies canonical first `+0.0`/bitwise adjacency, copies component/base/cumulative unchanged, and uses bitwise-last total. Cursor sampling adds no position/arithmetic/query/budget work. Under pending P-C1 its request ID equals presented cursor-state ID. Publicly manufactured records/fields, mixed/zero/subset/nonfinite states are unconstructible. EMPTY has only each `series_id` and no total value. |
| GUI indicator transitions | Pending P-C1 proposes: activation/move/deactivation atomically increment state and immediately clear before requesting; config is checked first on result consumption; inactive ignores all results; active READY/STALE compare content ID while EMPTY/FAILED/section failure compare current-attempt ID; mismatch performs no mutation, and match applies or clears the governed scope. Rapid A/B/C ordering, old absence/failure, movement→BUSY stale, and deactivation cannot restore prior state. This row is not approved D9. |
| Metadata/table prepass | Allocation-free MAIN-then-PREVIEW registry validation and fixed-slice admission maps every position/disposition first. One independently frame-accounted table for all accepted/rejected groups allocates next; acquisition follows. A rejected unit has no source or unit staging/geometry/QRhi/retained allocation. |
| Result ownership | One repo-local table-specific counted block has atomic refcount, immutable checked layout metadata, padding, and payload. Successful allocation increments exact `renderer_owned_requested_live_bytes` (sole CPU-cap oracle) and `allocation_requested_live_bytes` (lifetime evidence); publication decrements only the former, copies/non-final destruction neither, and final deallocation only the latter. Private retained/staging allocations never transfer/export; internal aliases change neither counter and only actual deallocation decrements both. Any intentional transfer uses the public-table transition. Failed allocation changes neither; no double debit. Allocator bookkeeping/rounding and physical/OS memory are separate evidence. |
| Fragment/frame/resident budget | Pending P-D2 endpoint/breakpoint excess is `FAILED(FRAGMENTATION_BUDGET)`; pending P-D15 admission is `FAILED(FRAME_BUDGET)`, cap excess `FAILED(RESIDENT_BUDGET)`, and in-cap API failure `FAILED(RESOURCE_ALLOCATION_FAILED)`. No case emits a partial group, and every P-D15 budget/allocation failure removes target stale eligibility. |
| Result storage fault | The fixed allocation-free `stack_result_section_t` publishes `FAILED(RESULT_STORAGE_BUDGET)` for checked arithmetic/exact prospective cap excess or `FAILED(RESULT_STORAGE_ALLOCATION_FAILED)` for admitted allocator failure. There is no table/acquisition/geometry; all stack retention clears while ordinary D9 output remains unchanged. |
| Post-composition failure payload | Empty intersection and nonfinite/fragmentation/numeric/output/resident/CPU/RHI failures have no content frame/cursor ID, value, rendered range, or indicator batch; each acquired member's single terminal CURRENT_OBSERVATION is the only permitted payload even if selected READY/window derivation completed earlier. |
| Main/preview admission | Pending P-D15 admits all MAIN units by ascending lowest ID before PREVIEW; previews cannot change MAIN admission. Every accepted unit receives an immutable `{M,V_limit,H_limit}` slice, so low versus full actual use by one unit leaves every later unit identical without shared runtime balance, slack transfer, refund, or readmission. |
| Group topology update | One `apply_series_updates` publishes the complete new group in one map revision. Individual add/remove commits intermediate topology and may render a valid partial membership; documentation/tests require batching when atomic topology matters. |
| One sequence/policy/view mutation | Cache invalidates once; composition runs once, base geometry uploads once, and—when LINE is present—the separately keyed padded boundary buffer uploads once. A style-only LINE change performs no recomposition/base upload and updates only boundary/draw state. |
| Unchanged second frame | With stable nonzero sequences and non-conservative semantics: zero composition, zero allocation, zero group upload, identical pixels/commands. Otherwise bounded recomposition occurs and records its disable reason. |
| Preview | Uses independent preview sources/LODs and same cumulative contract. |
| Indicator | Samples stored component, base, and cumulative independently from the frozen rendered event/grid representation, with A's exact rules or B's finalized-grid interpolation/displaced STEP rules. It performs no cumulative add/reconstruction, so absorption such as `1e16+1` preserves component `1`; markers use sampled cumulative and match rounded pixels. |
| Custom QRhi layer on a member | `FAILED(CUSTOM_LAYER_IN_STACK)`; no raw-coordinate custom draw is mixed with cumulative geometry. |
| Scalar/range-only member | `get_value` defines contribution; missing scalar is `FAILED(STACK_REQUIRES_SCALAR_VALUE)`; GLOBAL never sums draw envelopes. |
| Singleton/all-NONE group | Fewer than two is `FAILED(STACK_REQUIRES_TWO_MEMBERS)`; all NONE is `FAILED(STACK_GROUP_HAS_NO_VISIBLE_STYLE)`; an individual NONE still contributes. |
| Boundary LINE storage | Four-binding prev/p0/p1/next consumes separately keyed padded cumulative-top data; updates remain within `H_limit`. |
| Zero/unstable sequence or conservative access identity | Correct bounded recomposition occurs and a counter identifies why cache reuse was disabled. |
| Signed timestamp extremes | Merge/window logic is overflow-safe and preserves ordering. |
| K={2,8,32}, W={800,3840} | P-D15 requires ordinates within `M`, `V_observed<=V_limit`, `U_observed<=U_limit`, bytes within `H_limit`, and fixed frame totals, or exact `FAILED(reason)`. |

## Final recommendation

Execution status and the immediate Checkpoint 2.1 gate live only in
`VNM_PLOT_IMPLEMENTATION_PLAN.md`. Architecturally, consolidate D11-D14
source/frame semantics before stacking; otherwise the stack would multiply
snapshot, range, interaction, and lifetime inconsistencies.

The stack architecture itself is viable without relating source timestamps or
LOD ladders: independently select bounded source-local LOD windows, use the
eventual D4-selected bounded event-union or shared-grid evaluator, accumulate
double-precision cumulative bands, and upload one cached group representation.
The plan register—not this recommendation—identifies approved portions of
D1-D3/D5-D15. D4 and
P-S1/P-D7/P-D2/P-D6/P-Q1/P-C1/P-R1/P-D15 remain owner decisions. The
architecture is viable if all eight pending proposals are explicitly disposed,
recorded together by review-clean consolidated A1, and each affected contract is
ratified before its RU. D4 remains the later evidence decision. This review
cannot manufacture either approval.

Scalar/range, duplicate, BUSY, style/custom-layer, cache, and budget text is
owner-approved only where the plan labels it approved. The precise proposals
remain executable review material, not decisions, until explicit ratification.
