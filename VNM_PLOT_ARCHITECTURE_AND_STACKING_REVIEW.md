# vnm_plot architecture and performance-first stacking review

Review target: `vnm_plot` at `2d94f0184cdfb099d8d8aafae5a0d955f03566ee`

Review date: 2026-07-10
Scope: existing architecture, correctness, duplication, performance, and a first-class stacked-series design for independently sampled and independently levelled data sources.

## Executive conclusion

**Failure headline: 19 failed or terminated executions were observed—nine CI jobs/workflows, one style gate, five local/environment diagnostics, and four report-validation commands; their provenance, recovery, and ownership are grouped below.**

`vnm_plot` has a sound foundation and does not need a rewrite. The data/layout/RHI/Qt target split, immutable render-boundary descriptors, held snapshot lifetimes, per-source LOD planning, nanosecond timestamps, and D3D11-compatible prepare/record split should be preserved. The highest-value work is to make the current frame plan the single producer for range, rendering, interaction, and the proposed stack composition; remove avoidable steady-state snapshots, CPU staging, and GPU uploads; and tighten source lifetime and status contracts.

The requested stack should be a core frame-planning capability, not a custom rendering layer and not an application-side precomputed `h` source. Each component selects its LOD independently. The planner then evaluates those source-local curves under a strict screen-space composition/output budget and emits cumulative bottom/top bands. Two bounded strategies remain viable: an event union exact for the selected curves, or a shared bounded screen grid with better per-input density but another approximation. Native-backend and visual A/B evidence must choose one before implementation. Both support unrelated timestamps/LOD ladders and bound composition/GPU output; current snapshot/window APIs may still copy, lock, or align against more history, so those costs must be measured and, if necessary, given a bounded source hook before stacks are enabled by default.

The recommended implementation order is:

1. Completed during review: publish the `vnm_msdf_text` contract to `master`, rerun the live integration, and preserve both the initial red and subsequent green evidence.
2. Establish truthful production counters and remove unchanged-frame uploads and test-only render-loop work.
3. Consolidate frame snapshots, range planning, point evaluation, source statuses, and ownership.
4. Add the bounded stack planner and its mathematical tests.
5. Add the stacked-band RHI path, Qt interaction/preview integration, and end-to-end performance gates.

No proposed feature should land on the strength of a microbenchmark alone. The acceptance gate must include full-frame p50/p95/p99, total GPU upload bytes, allocation count, snapshot/query work, producer wait time, and native-backend identity.

## Review method and governance

This review followed `varinomics-standards`, including claim classification, concrete remediation for every finding, observable verification, explicit reporting of failed gates, test-oracle-first sequencing, and performance evidence that spans the real end-to-end path.

Four initial model reviews were used, followed by a separate Codex claim audit and local verification:

- three high-reasoning Codex reviews covering architecture, data/render mechanics, and falsification;
- one different-model Claude Fable review launched with model `claude-fable-5`, read-only repository tools, and no MCP tools;
- a final Codex claim audit over the combined findings and competing stack designs;
- a local source trace, builds, tests, style gate, CI observation, and benchmark inspection.

External review artifacts remain outside this repository. The Fable output is at `C:\plms\invoking_claude_from_codex\runs\vnm_plot_fable_review_20260710_20260710_105351\outputs\01_vnm_plot_full_review.md`. This document is the governed synthesis: it incorporates supported findings and explicitly rejects suggestions that conflict with repository evidence, performance requirements, or the simpler canonical design.

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
4. A budgeted exact union is internally consistent only when “exact” is explicitly limited to the independently selected LOD functions and LOD selection enforces the input budget before union. A full-width grid per member would emit `K*W`, but a bounded grid with `N=floor(M/(D*K))` (and `D=1` for all-LINEAR input) also respects the same total output budget and can retain far more input density for large K. The default is therefore **unresolved** pending native-backend compose/bytes/p95 and visual spike/step A/B evidence.

The audit also confirmed that commit `e605b5f` deliberately removed LOD hysteresis. This report therefore does not reintroduce it.

## Existing architecture to preserve

These are positive findings, but each has a concrete preservation rule so subsequent work does not accidentally erase the benefit.

| Evidence | Assessment | Concrete preservation rule and verification |
|---|---|---|
| `CMakeLists.txt:286-366,415-478,553-580` | **Source-confirmed:** data, layout, RHI, and Qt Quick are separate link targets. | Put stack mathematics and evaluation in layout/core, QRhi resources in RHI, and UI storage/interaction in Qt. Add a dependency-direction CMake smoke that builds each narrow target; do not introduce Qt types below the Qt target. |
| `include/vnm_plot/core/types.h:294-352` | **Source-confirmed:** snapshots explicitly retain storage lifetime. | The frame-level cache and stack planner must retain the complete snapshot result until composition and render finish. Add a source whose storage dies immediately after snapshot return and verify ASan-safe rendering. |
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

The dependency repository's own `c03a350` push workflows passed on [Linux](https://github.com/imakris/vnm_msdf_text/actions/runs/29083205733), [macOS](https://github.com/imakris/vnm_msdf_text/actions/runs/29083205779), and [FreeBSD](https://github.com/imakris/vnm_msdf_text/actions/runs/29083205767). Its [Windows workflow](https://github.com/imakris/vnm_msdf_text/actions/runs/29083204930) failed at workflow level with zero jobs and no logs. Cause is **unresolved**: run a GitHub Actions-aware validator such as `actionlint` on `.github/workflows/ci-windows.yml`, inspect expression-context availability—particularly job-level `runner.temp`—and land/rerun a focused workflow-only correction. Do not guess at a product-code fix. This dependency workflow failure keeps Batch 0 open even though all eight dependent `vnm_plot` attempt-2 jobs are green.

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

Failure ownership is explicit: dependency publication and the four `vnm_plot` workflow reruns are complete; dependency Batch 0 still owns its jobless Windows workflow; Batch 0A owns the 14 style violations; Batch 2 owns the shadow warnings; local gate automation owns initialized MSVC/platform selection; and the benchmark-integrity batch owns cold-start instrumentation. The concrete recommendation is to delegate the remaining remediation groups to those responsible agents/batches and require each to return its named gate evidence before closure. The green `vnm_plot` attempt 2 does not erase the eight initial failures or their provenance.

## Prioritized architecture and performance findings

### P0 — The intentional cross-repository `master` oracle exposed delivery order; dependent CI recovered, dependency Windows CI remains open

**Classification: Source-confirmed delivery failure and partial recovery under an owner-approved policy.** Both fallback declarations deliberately use the owner's moving `master`; the first attempts correctly exposed that the consumer reached `master` before the required dependency contract did. After `c03a350` reached dependency `master`, all eight dependent `vnm_plot` attempt-2 jobs passed. The dependency's own Windows workflow still fails before creating jobs.

**Remediation:** completed for the consumer break: publish the sibling change to its `master`, keep `vnm_plot` tracking `master`, and rerun all dependent workflows. Keep Batch 0 open until the dependency Windows workflow passes, add resolved dependency commit metadata, and retain no-sibling/install/package consumption coverage. For future coupled changes, push the dependency contract first and confirm its CI before pushing the consumer; if live integration breaks, leave it red until the repositories are compatible rather than masking it with a revision pin.

**Performance relevance:** no direct runtime effect. Because the dependency intentionally moves, every benchmark artifact must record its resolved commit so before/after binaries remain attributable without changing the `master` policy.

### P1 — VISIBLE auto-range does not use the displayed LOD plan

**Classification: Source-confirmed correctness and performance issue.** `Auto_v_range_mode::VISIBLE` says it uses LOD selection (`include/vnm_plot/core/plot_config.h:40-47`), but `src/core/auto_range_resolver.cpp:432-439` selects LOD 0 for every mode except `GLOBAL_LOD`. The renderer chooses the actual visible LOD later in `src/core/series_window_planner.cpp:860-923`. This can scan full-resolution visible data and compute a range that differs from the curve actually displayed.

**Remediation:** introduce one internal `Frame_series_planner` before range/layout. It produces one immutable per-series/per-view plan containing source, selected LOD, snapshot/status, source window, drawable spans, semantics, and cache identity. Use full framebuffer width—not post-label `usable_width`—as the deterministic LOD-budget width for both range and renderer; final layout may still use `usable_width` for transforms, but it must not reselect LOD. `Frame_range_planner` consumes those plans for VISIBLE range, and `Series_renderer` consumes the same plans for upload/draw. Do not call the two planners independently or introduce an unbounded layout/replan cycle.

**Verification:** use a source with million-, ten-thousand-, and thousand-sample levels at width 1000. Assert range and renderer consume the same selected LOD and source sequence, and that each `(source, LOD)` is snapshotted once per frame.

### P2 — Unchanged frames restage and upload primary and line-window data

**Classification: Source-confirmed performance issue.** A valid snapshot leads to primary staging/upload in `src/core/series_renderer.cpp:1640-1805`. A custom layer requests a frame snapshot even when sequence and view are unchanged (`src/core/series_renderer.cpp:879-893`; `src/core/series_window_planner.cpp:382-391`), so unchanged data is uploaded. LINE rebuilds and uploads its padded window at `src/core/series_renderer.cpp:1921-2063`. The public profiler counts the primary upload at `src/core/series_renderer.cpp:1368-1435` but omits the line-window upload at `2058-2062`.

**Remediation:** add two explicit keys to each view state:

- `sample_upload_key`: source identity, stable nonzero sequence, selected LOD, origin, access/semantics key, source window, drawable-span hash, nonfinite behavior, hold behavior, and view kind. A zero/unstable sequence or conservative semantics disables cross-frame reuse and increments a reason counter; treating zero as a reusable version risks stale geometry;
- `line_window_key`: primary-content revision incremented on every content upload, interpolation, and segment spans. Allocation generation alone is insufficient because an in-place primary upload changes content without reallocating the VBO.

When the relevant key matches, retain the buffer even if a current CPU snapshot was required for a custom layer. Continue calling custom-layer prepare as its ABI requires. Publish primary, line-window, and total sample upload counts and bytes.

**Verification:** after warm-up, static LINE and LINE+custom-layer frames with stable nonzero sequence/non-conservative semantics must report zero library-owned primary/LINE CPU restaging and uploads, with identical library pixels. An arbitrary custom layer may legitimately upload in each `prepare()`; report that separately and never claim it is zero when unobservable. A data-affecting mutation for LINE must cause one primary upload and one derived line-window upload, each exactly once; styles without LINE perform only their required primary upload. Zero/unstable/conservative inputs must recompute rather than reuse. Change `tests/test_qrhi_layer_lifecycle.cpp:1033-1049`, which currently expects an unchanged second-frame upload, only after the new oracle is owner-approved.

### P3 — Snapshot caching is per series instead of per frame/source/LOD

**Classification: Source-confirmed mechanism; cost is inferred until measured.** Each series state owns its own cache (`include/vnm_plot/rhi/series_renderer.h:147-152`; `src/core/series_window_planner.cpp:248-269`). Multiple series using the same source and LOD can therefore obtain multiple copies and potentially observe different sequences.

**Remediation:** make the `Frame_series_planner` own a cache keyed by `(Data_source*, LOD)`, storing the complete `snapshot_result_t` including status and hold. Every descriptor and snapshot hold remains alive for the frame, so the object address cannot be recycled during that cache lifetime. Do not use shared-pointer control-block ownership as this object key: aliasing pointers can share a control block while naming different source subobjects. Clear the cache only after every range, custom-layer, composition, and render consumer finishes. A stack shares this cache with ordinary series.

**Verification:** ten series sharing one source/LOD produce exactly one snapshot request; two selected LODs produce exactly two; all plans report the same sequence for a shared key. A copy-on-snapshot source should show the removed bytes and time in full-frame counters.

### P4 — UI range and indicator paths duplicate and diverge from renderer semantics

**Classification: Source-confirmed correctness and performance issue.** `auto_adjust_view()` snapshots/scans LOD 0 at `src/qt/plot_widget.cpp:1106-1147`. Indicators snapshot LOD 0 at `1312-1371` and use `bracket_timestamp()`, which infers order from endpoints and binary-searches (`include/vnm_plot/core/algo.h:615-675`) despite `Data_source` supporting `UNORDERED`. These paths also bypass the renderer's complete nonfinite policy.

**Remediation:** add one core `resolve_sample_at` evaluator and one public `Data_source::query_sample(timestamp, mode, context)` status-returning hook. The default hook may snapshot and evaluate; indexed sources override it. Required behavior:

- ascending/descending: O(log n);
- unknown: cache monotonic classification by identity/sequence/access semantics;
- unordered nearest: O(n) correct global nearest;
- unordered interpolation: explicit unsupported unless the source supplies a direct semantic query;
- all paths: use the same interpolation, duplicate-timestamp, hold, and nonfinite producer used for rendering.

Route `auto_adjust_view()` through the same range producer used by frame auto-range. The stack planner and stacked indicator must call the same evaluator rather than implement another interpolation loop. For pixel-parity indicators, the UI publishes the cursor timestamp to the renderer; the renderer evaluates it from the same held frame plan/snapshot and publishes immutable indicator values back, accepting one-frame latency. Do not independently query after render, because the source may have advanced. A direct source query remains valid for non-pixel-parity operations only when selected LOD and expected sequence are supplied and mismatch is discarded/retried.

**Verification:** unordered timestamps `[0,90,10,100]` queried at 11 must resolve to 10 in nearest mode. Cover descending data, duplicate timestamps, every nonfinite policy, all-empty/nonfinite windows, and a million-sample indexed source that proves bounded direct-query work. Tooltip values must match the displayed selected LOD.

### P5 — `Data_source::snapshot()` erases EMPTY, BUSY, and FAILED distinctions

**Classification: Source-confirmed design issue.** `include/vnm_plot/core/types.h:729-733` converts non-ready outcomes into an empty snapshot. The production users are limited to `src/core/auto_range_resolver.cpp:60` and the Qt paths at `src/qt/plot_widget.cpp:1111,1320`.

**Remediation:** remove the wrapper atomically and migrate each user to `try_snapshot()`:

- EMPTY: valid absence of data;
- BUSY: keep the last complete operation/view without mutation and increment a busy observation;
- FAILED: suppress that operation and report once through the configured diagnostic path;
- READY: consume while retaining its hold.

Do not leave a deprecated alias, because it would retain the ambiguity.

**Verification:** a four-state source must prove no view mutation on BUSY, an observable diagnostic on FAILED, no draw on EMPTY, and normal consumption on READY.

### P6 — Non-owning `Data_source_ref` can outlive its source across the render boundary

**Classification: Source-confirmed lifetime risk.** The raw reference path is stored by `include/vnm_plot/core/types.h:787-829`, used by both preview and ordinary series at `include/vnm_plot/core/types.h:955,1001-1032`, exposed by `include/vnm_plot/core/basic_series_builder.h:67-71`, and cloned for asynchronous rendering without extending source lifetime. Production examples use it, as does the benchmark.

**Remediation:** replace `Data_source_ref` in both `series_data_t` and `preview_config_t` with `std::shared_ptr<Data_source>`, then remove `set_data_source_ref`, `data_source_ref`, and the builder overload in one public API change. Migrate `hello_plot`, `function_plotter`, tests, and benchmark; do not add a no-op deleter compatibility bridge. In the benchmark, make `Benchmark_data_source` own a `shared_ptr<Ring_buffer<T>>` and let generator/window share it. Merely changing the source from `unique_ptr` to `shared_ptr` while it retains `Ring_buffer&` would allow the plot-held source to outlive its buffer during QObject teardown. Shared-pointer copying occurs when immutable descriptors are published, not per sample or draw.

**Verification:** ASan test releases every caller handle after adding ordinary and preview series and renders safely until removal; a benchmark teardown test destroys the window/generator while the render descriptor briefly remains held; TSan stress replaces/removes series during render synchronization. `rg "Data_source_ref|set_data_source_ref|data_source_ref\\("` must return no production, example, benchmark, or public-header use.

### P7 — Floating timestamp-member conversion can invoke undefined behavior

**Classification: Source-confirmed correctness issue.** `include/vnm_plot/core/access_policy.h:63-77` multiplies a floating member by `1e9` and casts directly to `int64_t`. NaN, infinity, or out-of-range finite values make the conversion invalid/undefined. README and typed API tests make floating seconds an adopted behavior, so it cannot simply be ignored.

**Remediation:** performance-first preference is to require integral nanoseconds in the member-pointer hot path with `static_assert`, and provide a checked ingestion conversion so floating timestamps normalize once. If the convenience API must remain, return value-plus-validity, check finite/range before conversion, and propagate an invalid window; do not clamp or use `INT64_MIN` as a sentinel because it is a valid timestamp.

**Verification:** typed and erased tests for NaN, infinities, boundary values, and one-step outside boundaries under UBSan. Benchmark accessor dispatch and full-frame planning to confirm the chosen contract adds no hot-loop regression.

### P8 — Test-only vectors execute in the production draw loop

**Classification: Source-confirmed easy performance win.** Four vectors labelled test instrumentation in `include/vnm_plot/rhi/series_renderer.h:181-186` are cleared per frame (`src/core/series_renderer.cpp:660-667`) and receive four `push_back` calls per draw (`1552-1561`).

**Remediation:** guard members and all writes with the existing `VNM_PLOT_ENABLE_TEST_HOOKS`; build the lifecycle test against a hook-enabled library/object and normal releases without it. Provide a test accessor instead of `private`/`public` preprocessor rewriting.

**Verification:** normal Release objects contain no hook vectors/writes, hook-enabled tests retain their ordering assertions, and production draw/upload output is unchanged.

### P9 — Policy identity and series identity can invalidate too much or reuse the wrong state

**Classification: Source-confirmed invalidation mechanism; allocator-ABA consequence is inferred.** Access cache identity includes policy object address (`include/vnm_plot/core/types.h:102-129,569-581`), while descriptors are cloned on update (`src/qt/plot_widget.cpp:178-193`). Color-only updates can therefore invalidate data caches. Pointer-address identity also risks accidental reuse after destruction/allocation at the same address.

**Remediation:** key geometry state by stable series ID plus a monotonic `geometry_revision` that changes when source, access/layout semantics, interpolation, nonfinite/hold behavior, or other data-affecting fields change. Preserve that revision for color/label-only clones only when access semantics are non-conservative or the caller supplied a stable semantics key/revision. A conservative callable clone with no comparable semantic identity must invalidate even on an otherwise visual-only full-descriptor update. For member/offset semantics, key by byte layout and semantic value/revision rather than the cloned policy object's address. Source `shared_ptr` ownership provides lifetime only, not cache identity.

**Verification:** a color/label-only update with stable explicit semantics preserves `geometry_revision` and causes no snapshot, range rescan, VBO upload, or custom-layer recreation; a conservative callable without a semantics key invalidates; callable replacement or semantic revision invalidates once; remove/re-add creates a new registry incarnation and never reuses prior GPU/cache state under allocator churn.

### P10 — Qt synchronization copies configuration and the full series map on unchanged frames

**Classification: Source-confirmed operation; cost is inferred.** `src/qt/plot_renderer.cpp:205-216` copies `Plot_config` and the complete map before consulting revision state.

**Remediation:** compare `m_config_revision` before copying configuration. Add a series revision incremented under the existing series mutex by add/remove/clear/apply; copy the map only when it changes. Keep the current locking model rather than adding an event bus or model hierarchy.

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
- remove `cached_snapshot_hold` if it merely duplicates the hold already carried by the snapshot result;
- reject/erase null descriptors in `apply_series_updates` rather than retaining a map key whose renderer resources survive because cleanup only sees key absence;
- make `Series_view_plan` own or directly expose the canonical `sample_window_t` instead of manually copying equivalent fields at `src/core/series_renderer.cpp:977-1004`;
- cache LOD scale topology only after declaring it immutable or adding `lod_layout_revision()`; otherwise the cache would be incorrect.

**Verification:** focused unit tests for floor division signed extremes, null replacement resource cleanup, and plan/window field parity. Add a topology revision invalidation test only if topology caching/revision is actually introduced. Do not create a shared helper for the two existing `checked_size_add/product` copies; two uses are below the governance N=3 consolidation threshold.

## First-class stacked-series contract

### Public API: extend ordinary series registration

**Proposed.** Keep one canonical series registry and add one optional immutable group identifier:

```cpp
std::optional<int> stack_group_id;
```

Add `Basic_series_builder::stack(group_id)` and `clear_stack()`. An enabled series with no membership behaves exactly as today. Enabled members with the same group form one stack in ascending existing series-ID order. Existing source, access policy, interpolation, nonfinite behavior, color, label, preview source, and style remain per series. Reuse the existing ID as order instead of adding a second ordering concept; add a distinct order property only if an actual caller later needs to reorder layers without changing IDs.

This is preferred over a separate `Stack_data`/`add_stack()` object because it reuses the canonical registration/update/lifetime path and avoids a second descriptor hierarchy. `apply_series_updates` is the documented atomic way to change multiple members. Individual `add_series`/`remove_series` calls are not group-atomic: they commit each intermediate topology, and a still-valid two-of-three-member group can render that intermediate partial sum. Callers requiring atomic topology must batch. If the owner requires atomicity regardless of caller behavior, re-evaluate a group-owned immutable descriptor before implementing this API. A member must provide both timestamp and scalar `get_value` access; `Data_access_policy::is_valid()` alone is insufficient because range-only access cannot define `cj(t)`. `cj` always comes from `get_value`, even when a draw-range accessor also exists. Missing either required accessor returns `STACK_REQUIRES_SCALAR_VALUE`; any missing source invalidates the current group. Within one immutable topology, silently dropping a failed member is forbidden because it would display a different sum.

An enabled member with `Display_style::NONE` still contributes mathematically but emits no band, boundary, or dots of its own; later cumulative members and the final total still include it. `enabled=false` is the canonical way to exclude a component from the sum. This keeps visibility and mathematical membership explicit rather than making style selection mutate data semantics. A group requires at least two enabled members (`STACK_REQUIRES_TWO_MEMBERS`) and at least one supported visible built-in style across the group (`STACK_GROUP_HAS_NO_VISIBLE_STYLE` otherwise). DOTS, LINE, and AREA combinations are the supported first-version styles.

For the first version, a stack member with `Qrhi_series_layer` is explicitly unsupported and invalidates the group with `CUSTOM_LAYER_IN_STACK`. The custom-layer ABI describes one ordinary planned window and has no cumulative bottom/top contract; silently drawing it at raw component coordinates would make it disagree with the stack. A future stacked custom-layer ABI requires a separate owner-approved use case and benchmark.

### Mathematical definition

**Proposed; owner approval required before tests are treated as oracle.** For ordered component functions `c1...cK`:

```text
S0(t) = 0
Sj(t) = Sj-1(t) + cj(t)
```

Layer `j` fills the band between `Sj-1` and `Sj`. Its boundary line is `Sj`; the last boundary is the requested `h`. Values are accumulated in `double`, checked, and narrowed to float only for GPU storage.

- Negative values use algebraic stacking: a negative component descends from the preceding cumulative value. Do not silently use a separate positive/negative baseline because then the final top is not `f+g`.
- AREA draws the bottom/top band. LINE draws cumulative boundaries. DOTS draw cumulative knots. Zero is only the `S0` baseline.
- Auto-range includes every bottom and top, not only `h`. For `f=100, g=-100`, the visible range must still include the intermediate 100 band.
- The feature means “sum of the selected displayed LOD approximations.” The source API does not state whether a coarse LOD sample is a mean, extrema envelope, or exact raw sample, so claiming raw LOD-0 analytic truth would be false.

### Timestamp, interpolation, domain, and status semantics

**Proposed; owner approval required for the intersection-domain rule.** Each member is evaluated as its own selected piecewise function:

- Candidate A builds the ordered union of selected member timestamps. Candidate B builds a view-derived shared grid. Neither may match array indices, require equal timestamps, or choose one operand's grid.
- LINEAR evaluates piecewise linearly at every emitted position. STEP_AFTER is right-continuous: a value becomes active at its timestamp and holds until the next. Candidate A emits a left-limit event and then a right-value event at a changing step timestamp; without both, geometry would bridge the discontinuity. Candidate B evaluates right-continuously at grid positions and accepts the measured/signed-off screen-space displacement described by its approximation contract. Mixed modes are allowed.
- Equal timestamps collapse to the sample with the highest physical source index, regardless of traversal direction, making the winner identical for ascending and descending planning and simultaneous changes deterministic.
- ASCENDING and DESCENDING use direction-aware cursors. UNKNOWN performs one cached classification per identity/sequence/access key only when sequence is stable and nonzero; otherwise it scans once for the frame-held snapshot and does not reuse that classification across frames. Truly UNORDERED data returns explicit `UNORDERED_STACK_INPUT`; sorting every frame is both expensive and semantically ambiguous for a function.
- The group domain is the intersection where every component has a value. There is no left extrapolation. Right extension is allowed only when each otherwise-ended component explicitly uses hold-last-forward.
- BREAK_SEGMENT intersects valid spans across components. SKIP removes the invalid knot and connects its valid neighbours. REPLACE_WITH_ZERO contributes zero. REJECT_WINDOW rejects the whole group window.
- EMPTY yields an empty group. FAILED suppresses the group and reports once. Define a structural key containing registry incarnations, membership/order, source objects, access semantics, view, and configuration but excluding live sequences. On BUSY, ignore every newly acquired operand and reuse the previous complete group wholesale only if that structural key matches; otherwise suppress. Never combine fresh and stale generations.
- Snapshots from different sources are individually stable, not transactionally simultaneous. Different LOD snapshots of the same source have no stronger atomicity guarantee. The contract must say this. Applications needing atomic cross-signal or cross-LOD publication must publish through one coordinated snapshot/revision contract.

### Bounded independent LOD planning and unresolved sampling strategy

**Proposed common constraint; strategy unresolved.** Neither “merge all LOD 0 samples” nor “give every member a full one-sample-per-pixel input before an unbounded union” is acceptable. The first grows with history; the second can emit approximately `K^2*width` cumulative band samples.

Let:

- `W` be full framebuffer plot width before label layout;
- `K` be enabled group members;
- `C` be an internal calibrated total band-samples-per-pixel budget, initially 2;
- `D` be 1 for all-LINEAR groups and 2 when STEP_AFTER output can require paired left/right events;
- `M = floor(C * W)` be the hard emitted band-ordinate-pair budget;
- `A = align_up(64, QRhi uniform-buffer alignment)` be the actual per-member uniform allocation/update allowance for the active backend;
- `H = 32*M + A*K + 64` be a conservative first-version hard total stack-upload byte cap derived below from base and LINE-boundary record formats, not a separately tuned performance knob.

Extend the planner request with separate `render_width_px` and `max_visible_samples`; do not overload the shader/view width. Every strategy selects each source independently from its own ladder and includes interpolation brackets/synthetic hold knots in its input count. If even a coarsest complete window exceeds the chosen per-input cap, return `LOD_BUDGET_EXCEEDED` rather than dropping brackets or falling back to raw history.

**Candidate A—budgeted event union.** Let `B=floor(M/(D*K^2))`. Require `B>=2`. With each input at most `B`, union `U<=K*B`, events `E<=D*U`, and output pairs `K*E<=M`. It is exact relative to selected piecewise curves, including step discontinuities, but input density falls quadratically with K; for `W=800,K=32,C=2`, it cannot represent a segment and returns `STACK_BUDGET_TOO_SMALL`.

**Candidate B—bounded shared grid.** Use the same D factor and let `N=floor(M/(D*K))` shared grid timestamps; require `N>=2` so interpolation brackets and a drawable segment have an unambiguous budget, otherwise return `STACK_BUDGET_TOO_SMALL`. Cap each selected source window at N. Evaluate every component with monotonic cursors. For STEP_AFTER, when the sampled value changes, emit left/right events at the grid transition where the change is observed, so `E<=D*N`, output `K*E<=M`, and CPU work is `O(sum(ni)+K*E)`. At `W=800,K=32,C=2`, an all-LINEAR group retains 50 grid values per band; a step-capable group retains 25 grid positions and up to 50 events. It adds a screen-space approximation: narrow spikes can alias and a true step between grid points is displaced to the sampled transition.

Do not choose a default from asymptotic argument alone. Prototype both outside the public API with identical independent LOD selection and hard `M/H` counters. Compare native-backend compose time, total bytes, allocations, producer wait, and full-frame p50/p95/p99 for K={2,8,32} and W={800,3840}; visually inspect phase-shifted narrow spikes, mixed LINEAR/STEP_AFTER discontinuities, gaps, and cancellation. The owner selects one strategy before its tests become product oracle; do not ship two speculative modes.

For either candidate, final buffer construction counts every update/padding byte against `H`; if actual record formats change, recompute the `sizeof`-based bound and tests in the same commit. Return `STACK_OUTPUT_BUDGET_EXCEEDED` rather than exceeding `M` or `H`. Composition/output is screen-bounded; snapshot copying, locking, and full-snapshot window alignment are not yet proven bounded.

`C` should remain internal until native-backend quality/performance results show that callers need control. Verify the hard output/byte bounds and explicit budget failures for both prototypes.

Do not add LOD hysteresis as part of this feature. Commit `e605b5f` deliberately reverted hysteresis; absent a new owner decision and evidence, selection must remain deterministic under the current policy.

### Shared frame plan and cache

**Proposed.** Add internal `Frame_series_planner` and `Stack_planner` producers in the layout/core target. The frame dependency flow is:

```text
immutable series registry
        |
frame snapshot cache: (Data_source*, LOD) -> complete result/hold
        |
independent per-series plans -----> ordinary VISIBLE range and renderer
        |
stack composition plans ----------> stacked range and stacked renderer
        |
final layout, upload, draw, preview, and interaction observations
```

Give each registry entry an incarnation that survives ordinary descriptor clones and changes only on remove/re-add. Use the map's series revision only to detect/publish a new immutable registry snapshot; do not include it in geometry equality. The stack geometry key includes ordered registry incarnations and geometry revisions; source object addresses and sequences; selected LODs; source windows/spans; access semantic identities/revisions; interpolation, nonfinite, and hold behavior; main/preview kind; visible time range; time origin; and pre-layout framebuffer width. Exclude color, label, and built-in draw-style state from composition/base-upload identity; those change draw commands/uniforms or a separately keyed boundary buffer only. An identical geometry key means no merge, allocation, or base geometry upload.

Composite reuse is allowed only when every source supplies a stable nonzero sequence and every access policy supplies non-conservative semantic identity/revision. Otherwise correctness wins: recompute the bounded group and record the reason reuse was disabled. BUSY follows the separate structural-key rule above and never consumes a partially fresh operand set.

Reuse vectors and preserve capacity so steady-state composition allocates zero heap blocks. Do not implement suffix-only incremental recomposition from `current_sequence()`: the current contract does not promise append-only immutable prefixes or report the first changed index. If profiling later proves this necessary, first add an explicit source change-range contract, then key and test it.

### GPU representation and draw order

**Proposed.** Use one changed-group base upload rather than duplicate bottom/top timestamps in K ordinary-series VBOs:

- one contiguous `float t_rel[E]` block, where E is the final event-union or grid-point count, plus an event-side stream when Candidate A has equal-timestamp left/right ordering;
- K contiguous `{float bottom, float top}[E]` blocks;
- small span/offset metadata for broken domains.

The base time/ordinate payload is `4E+8KE`; for `K>=2` and `K*E<=M`, it is at most `10M`. Event-side data costs at most `M/2`, and two-`uint32_t` span records cost at most `4M`, so base geometry/metadata is below `14.5M`. A stacked-band vertex shader uses D3D11/SM5-compatible vertex attributes and emits quads between bottom and top without clamping their order. Candidate-A equal-timestamp left/right events emit a vertical edge instead of a diagonal bridge.

Boundary lines need an explicit derived buffer; the ordinary renderer's D3D11 path requires `prev,p0,p1,next` and cannot read the proposed blocks without edge padding. For each visible LINE member and group span of length at least two, stage compact `{t_rel, cumulative_top}` pairs as `[first, samples..., last]`. Bind the same buffer four times with 8-byte stride and offsets 0/8/16/24 to a stack-boundary shader. The unpadded values cost at most `8M`; because drawable LINE spans contain at least two events, all duplicate edge padding costs at most another `8M`. Key this buffer separately by base-composite content revision, LINE-member mask/style revision, and spans. A style change may rebuild the boundary buffer but must not recompose/reupload base bands.

The worst-case base `14.5M` plus boundary `16M` fits the rounded `32M` geometry allowance in `H`; backend-aligned uniforms/header use the remaining explicit `A*K+64`. Record A and every actual aligned update, and hard-fail if they exceed `H`. Draw filled bands in component order, then cumulative boundaries so the final line visibly represents `h`. Prepare/upload outside the render pass; bind/draw inside.

Before committing this layout, benchmark it against the simpler 16-byte interleaved per-layer representation on the supported backends. Accept the group layout only if it reduces total bytes/CPU staging without increasing draw or binding cost enough to regress full-frame p95. Regardless of layout, counters must separately report stack time/value bytes, metadata bytes, upload count, and vertices.

Stacking must not use `Qrhi_series_layer`: that API receives one already-planned series window and would have to duplicate multi-source snapshots, LOD selection, range, caching, and failures. Keep the custom-layer ABI unchanged.

### Auto-range, preview, and indicators

**Proposed.** The shared stack plan computes min/max for every emitted bottom/top during its one composition pass. VISIBLE range consumes exactly those extrema.

GLOBAL and GLOBAL_LOD should not construct full-history composite geometry. Extend `data_query_context_t` with an explicit value projection: existing ordinary rendering may request draw extent, while stacks request `SCALAR_VALUE`, which reads `get_value` even when `get_range` exists; an override that cannot honor the projection returns UNSUPPORTED and uses the canonical scalar fallback. GLOBAL uses each source's LOD 0; GLOBAL_LOD uses each source's own coarsest level, without relating ladder indices. First compute every selected level's effective time domain, including explicit right hold, and intersect them; a disjoint intersection returns EMPTY. Restrict each scalar range query to that common domain, then compute conservative prefix bounds by summing minima/maxima for every prefix plus zero. This cannot clip a defined band, though it may remain wider because extrema and internal valid spans need not overlap simultaneously; exact internal-gap composition is intentionally avoided for global modes. Cache domain and scalar results by source/range semantic keys.

Preview invokes the same planner independently with effective preview sources, LODs, width, and range; it must not fall back to unrelated zero-based areas.

At the cursor, evaluate the published rendered representation and return, per member, `component_y`, `stack_base_y`, and cumulative `y`, plus `stack_total_y`. Candidate A uses its emitted event-union plan; Candidate B interpolates/evaluates its emitted grid/event output, not a fresher or more exact underlying curve. Place markers on cumulative tops. Never query raw LOD 0 or bypass the selected representation when producing a pixel-parity tooltip.

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

### Batch 0 — Restore dependency and CI integrity (consumer recovered; dependency Windows still open)

Files/tools: `vnm_plot/CMakeLists.txt` and package smoke configuration; separately authorized sibling `vnm_msdf_text/.github/workflows/ci-windows.yml`; `actionlint` (or equivalent GitHub Actions-aware validation) plus an actual GitHub workflow rerun.

Deliverable: required dependency contract is present on `master` and all eight dependent `vnm_plot` jobs are green. Remaining deliverables are a valid green dependency Windows workflow, resolved dependency commit recording, and retained no-sibling install/consumer smoke.

Gate: `actionlint`/workflow validation passes and dependency Windows creates real jobs; exact fetched revision is recorded; dependency Linux/Windows/macOS/FreeBSD workflows pass; dependent `vnm_plot` Linux/Windows/macOS/FreeBSD text-OFF/text-ON pass.

### Batch 0A — Close existing style debt without semantic mixing

Files: `src/core/series_renderer.cpp:95`, `src/core/types.cpp:47-53,389-394`, and `tests/test_msdf_lcd_shader_reference.cpp:217-225`.

Deliverable: only the 14 reported case-layout/indentation corrections, with the five current-commit and nine older/perpetuated violations preserved in the commit message provenance. Do not mix stack or hot-path changes into this batch.

Gate: rerun the governed style pipeline from its first stage and require it to pass; run the initialized Release build and 21-test CTest gate to prove formatting changed no behavior. The shadowed time constants are handled in Batch 2 because their removal changes source tokens beyond formatting.

### Batch 1 — Truthful hot-path observations and unchanged-frame reuse

Files: `include/vnm_plot/rhi/series_renderer.h`, `src/core/series_renderer.cpp`, QRhi lifecycle tests, benchmark profiler/reporting, `benchmark/ARCHITECTURE.md`, benchmark CMake/workflows.

Deliverable: test hooks absent from production, complete primary/line upload counters, keyed unchanged-frame buffer reuse, actual backend metadata, deterministic CI smoke.

Gate: static second frame has zero library restage/upload; a LINE mutation uploads primary content once and derived line-window content once; native backend and total bytes recorded; non-stack pixels/draws unchanged. Under identical Release build, backend, finish mode, dimensions, sources, seed, and instrumentation, compare before/after full-frame p50/p95/p99, GPU-finished time, allocations, producer wait, and memory for static and live workloads. Accept only with the deterministic counter win and no statistically meaningful end-to-end regression.

### Batch 2 — Canonical ownership, status, evaluator, and frame plan

Files: `include/vnm_plot/core/types.h`, `basic_series_builder.h`, `access_policy.h`, `algo.h`, `time_units.h`, `src/core/types.cpp`, `series_window_planner.*`, new `frame_series_planner.*`, auto/frame range, `include/vnm_plot/rhi/series_renderer.h`, `src/core/series_renderer.cpp`, `plot_renderer.cpp`, `plot_widget.cpp`, `examples/hello_plot`, `examples/function_plotter`, `benchmark/include/benchmark_data_source.h`, `benchmark/include/benchmark_window.h`, `benchmark/src/benchmark_window.cpp`, `README.md`, and focused tests.

Deliverable: shared ownership only, explicit snapshot statuses, canonical point/range semantics, frame `(source,LOD)` cache, and VISIBLE/render plan identity. It also owns the bounded P14 cleanups: canonical `floor_div`, duplicate snapshot-hold removal, null-update erase, plan/window consolidation, canonical time constants, and conditional LOD-topology cache only if measured. README removes nonexistent colormap/config drift and documents the resulting source/query contract. No stack-only public metadata or unused stack planner lands in this batch.

Gate: ASan/TSan lifetime tests, UBSan timestamp cases, unordered/nonfinite evaluator table, one snapshot per shared key, no unchanged Qt map copy, VISIBLE range/render parity, warning-clean initialized build, and all focused cleanup tests. Compare identical-workload before/after full-frame p50/p95/p99, allocations, producer wait, snapshot bytes/time, alignment scans, and memory for held zero-copy and copy-on-snapshot sources; require no statistically meaningful non-stack regression. If history growth increases acquisition work beyond the owner-accepted bound, stop and add/approve a bounded window-snapshot hook before Batch 3; do not add it speculatively.

### Batch 3 — Atomic end-to-end stack feature

Files: public series metadata/builder, new `src/core/stack_planner.{h,cpp}` (with internal placement consistent with the layout target), scalar range projection, frame-range integration, Qt registry/API/preview/indicator publication, series renderer state/commands, one stack-band shader and boundary path, CMake/QSB assets, focused math/RHI/interaction tests, example/QML, benchmark scenarios, and architecture documentation.

Deliverable: one usable public capability in the same governed batch—A/B-selected bounded composition strategy, independent LOD requests, cumulative bands, status/domain handling, VISIBLE/global scalar range, cached RHI rendering, preview parity, frame-pinned indicators, example, and representative benchmark. Internal commits may aid review, but no stack-only public metadata, unconsumed planner, or unused shader is pushed independently.

Gate: complete owner-approved acceptance matrix; hard band-ordinate and total-byte bounds; linear visit counters; zero steady allocation after warm-up; stable/non-conservative unchanged second frame zero composition/upload; unstable/conservative inputs perform bounded recomposition and expose the disable reason; shader bake and prepare/record lifecycle on every backend; non-stack counters bit-for-bit unchanged. Human approval is required for representative positive, negative, mixed-step, gap, and unrelated-LOD scenes before any golden image becomes an oracle. Compare full-frame native-backend p50/p95/p99, CPU plan/compose, GPU-finished time, total bytes, draws, allocations, snapshot bytes/time, alignment scans, producer wait, memory, and LOD changes. Require no statistically meaningful non-stack regression under identical builds; agree any numeric timing threshold from baseline evidence rather than inventing one.

## Stack acceptance matrix

The matrix becomes an authoritative product oracle only after the owner accepts the complete proposed contract, including scalar-only contribution/range semantics, duplicate winner, BUSY wholesale reuse, singleton/style/custom-layer limits, and explicit budget failures. Algebraic negatives and intersection-of-defined-domains are the two most visible mathematical decisions. Provisional mechanics/falsification tests may be written earlier, but they must not silently settle these policies.

| Case | Required observable result |
|---|---|
| Offset LINEAR timestamp grids | Candidate A includes both breakpoint sets; Candidate B evaluates both curves at every shared grid point. In either prototype, the final boundary is the double-precision sum at every emitted position. |
| Sampling strategy A/B | Candidate A and bounded-grid Candidate B use identical M/H and workloads but their defined B/N input caps; native compose/bytes/p95 plus spike/step visual evidence selects exactly one product strategy before oracle approval. |
| Mixed LINEAR and STEP_AFTER | Candidate A emits left/right at the true selected-curve timestamp; Candidate B emits left/right at its sampled grid transition. Both avoid a diagonal bridge, and indicators match the chosen representation; Candidate B's displacement is part of visual A/B approval. |
| Unrelated LOD counts/scales | Each source records an independently selected level satisfying its budget; no same-index assumption appears. |
| Coarsest LOD exceeds budget | Group reports `LOD_BUDGET_EXCEEDED`; there is no LOD-0 fallback or partial render. |
| Strategy budget too small | Candidate A B<2 or Candidate B N<2 reports `STACK_BUDGET_TOO_SMALL`; neither constructs ambiguous zero/one-point geometry. |
| Descending and UNKNOWN-monotonic | Direction-aware result matches ascending reference; stable nonzero sequence caches classification, while zero/unstable sequence scans once per frame-held snapshot only. |
| Truly unordered | Explicit group failure; no sort allocation and no misleading partial sum. |
| Duplicate timestamps | Highest physical source index wins consistently in ascending/descending planner and indicator. |
| Negative/cancellation `100 + -100` | Second band descends to zero; range still includes the first cumulative 100. |
| Disjoint/partially overlapping domains | Only intersection renders, except explicit right hold; no left extrapolation. GLOBAL/GLOBAL_LOD intersect selected-level domains first and return EMPTY when disjoint. |
| Nonfinite policies | BREAK/SKIP/ZERO/REJECT outcomes match the canonical evaluator and affect the whole sum where undefined. |
| BUSY/FAILED/EMPTY | BUSY reuses a previous complete group wholesale only on structural-key match and ignores all fresh operands; FAILED diagnoses/suppresses; EMPTY is empty; generations never mix. |
| Group topology update | One `apply_series_updates` publishes the complete new group in one map revision. Individual add/remove commits intermediate topology and may render a valid partial membership; documentation/tests require batching when atomic topology matters. |
| One sequence/policy/view mutation | Cache invalidates once; composition runs once, base geometry uploads once, and—when LINE is present—the separately keyed padded boundary buffer uploads once. A style-only LINE change performs no recomposition/base upload and updates only boundary/draw state. |
| Unchanged second frame | With stable nonzero sequences and non-conservative semantics: zero composition, zero allocation, zero group upload, identical pixels/commands. Otherwise bounded recomposition occurs and records its disable reason. |
| Preview | Uses independent preview sources/LODs and same cumulative contract. |
| Indicator | Reports component, base, cumulative top, and total from the published rendered event/grid representation, matching pixels even when Candidate B approximates the selected LOD curve. |
| Custom QRhi layer on a member | Explicit `CUSTOM_LAYER_IN_STACK`; no raw-coordinate custom draw is silently mixed with cumulative geometry. |
| Scalar/range-only member | `get_value` defines the contribution even when range exists; missing scalar value returns `STACK_REQUIRES_SCALAR_VALUE`; GLOBAL scalar projection never sums draw envelopes. |
| Singleton/all-NONE group | Fewer than two enabled members returns `STACK_REQUIRES_TWO_MEMBERS`; all members visually NONE returns `STACK_GROUP_HAS_NO_VISIBLE_STYLE`; an individual NONE member still contributes. |
| Boundary LINE storage | Four-binding prev/p0/p1/next shader consumes the separately keyed padded cumulative-top buffer; base plus boundary updates remain within H. |
| Zero/unstable sequence or conservative access identity | Correct bounded recomposition occurs and a counter identifies why cache reuse was disabled. |
| Signed timestamp extremes | Merge/window logic is overflow-safe and preserves ordering. |
| K={2,8,32}, W={800,3840} | Band ordinates remain within `M`, all uploaded bytes/metadata/padding remain within derived cap `H`, or an explicit budget status is returned. Separately report any snapshot/alignment cost that still grows with history. |

## Final recommendation

The dependent `vnm_plot` CI repair is complete; close the dependency's jobless Windows workflow as the remaining Batch 0 item. Then start `vnm_plot` implementation with Batch 0A and the unchanged-frame/counter batch; they are the clearest remaining easy wins and create trustworthy evidence for the larger change. Consolidate source/frame semantics before adding stacking; otherwise the stack would multiply existing snapshot, range, and interaction inconsistencies.

The stack architecture itself is viable without relating source timestamps or LOD ladders: independently select bounded source-local LOD windows, use the A/B-selected bounded event-union or shared-grid evaluator, accumulate double-precision cumulative bands, and upload one cached group representation. Before acceptance tests become product oracle, the owner must approve the complete proposed contract summarized in the matrix, including the sampling-strategy result. Its two most visible mathematical choices are:

1. accept algebraic negative stacking so the final boundary is always the actual sum;
2. accept intersection-of-defined-domains, with right extension only through explicit hold-forward.

The other scalar/range, duplicate, BUSY, singleton/style/custom-layer, cache, and budget policies are also explicit owner decisions, not silently settled implementation details. Every proposed choice has a concrete source boundary, failure behavior, cache identity, qualified performance bound, and executable gate.
