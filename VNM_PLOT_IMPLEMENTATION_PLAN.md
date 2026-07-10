# vnm_plot implementation plan

Plan date: 2026-07-10

Execution record updated: 2026-07-11

Source review: `VNM_PLOT_ARCHITECTURE_AND_STACKING_REVIEW.md` at `vnm_plot@2d94f01`

## Outcome

Proceed in five gated stages:

1. close the remaining CI/style baseline;
2. instrument, capture a native baseline, then remove known unchanged-frame work in the same renderer/benchmark batch;
3. consolidate ownership, status, query, range, frame-planning, and Qt publication contracts;
4. settle the stack contract and sampling strategy with a private benchmark A/B experiment;
5. land one complete end-to-end stack feature and run final gates.

Public stack exposure does not begin until Stages 2–4 pass and the owner selects D4 from the complete evidence. Private/core Stage 4 implementations of both candidates are explicitly allowed only to produce that evidence; they expose no public stack API, and the loser is deleted. The separately authorized dependency Windows repair may run in parallel with local `vnm_plot` work once dependent consumer CI and local product gates are green, but it remains a final-merge gate.

The initial 2026-07-10 Fable quota exclusion is historical and non-governing.
Subsequent work uses delegated implementation: Codex `/root` orchestrates, a
worker implements each review unit, and three independent xhigh reviewers
review the exact source hash until all three are clean. At least one reviewer in
every loop audits architecture, contracts, ownership, and missing primitives.
Fable is used only when that loop exposes genuine ambiguity. Local commits and
review branches may be pushed and PRs opened, but no batch merges to `master`
until its executable gate and review closure pass.

## Failure ledger

The architecture review records **19 failed or terminated executions** in [Observed failures and current gate status](VNM_PLOT_ARCHITECTURE_AND_STACKING_REVIEW.md#observed-failures-and-current-gate-status). Plan authoring added one failed `apply_patch` verification: a combined patch expected text that no longer exactly matched the staged draft. Codex `/root` owned the authoring error and recovered by rereading the file and applying smaller exact hunks, bringing the total to **20** before implementation began.

Batch 1B then added fourteen failed or terminated style executions, bringing the running total to **34**:

1. The aggregate `style_pipeline.py --write` invocation against the three originally named files advanced beyond the 14 switch fixes and attempted 737 insertions/826 deletions. Review caught `fix_hanging_indent.py` deleting a ternary expression's `?`, true arm, and `:` in `sample_statement_for_offset()` before commit. Codex `/root` owned the unsafe invocation, rejected the output, and restored all generated changes through `apply_patch`; no corrupting change was retained.
2. After applying only the authorized 14 switch fixes, the canonical no-write pipeline passed its first stage and failed at stage two on four previously hidden `else if` layout violations. A read-only audit then showed 22 of 27 rule groups red; those counts are diagnostic/provisional because earlier ordered fixes can affect later checks.
3. A worker's isolated formatter replay used the live repository as its effective working directory and reapplied scratch output there. The worker and Codex `/root` owned the scope error, compared the tree to the intended checkpoint, and restored the exact intended state before further work.
4. The prototype-block formatter's first retry terminated with `PermissionError` while an unrelated concurrent build held source files open. No formatter output was retained; Codex `/root` waited for the build to release the files and reran the same bounded checkpoint successfully.
5. One initialized build/test wrapper exited before execution because its PowerShell quoting was malformed. Codex `/root` corrected the invocation and retained the successful initialized Release build and CTest result rather than suppressing the failed command.
6. One read-only aggregate-inventory wrapper exited because its diagnostic parser expected the wrong message shape. No source was changed; Codex `/root` corrected the parser and retained the resulting all-rule inventory.
7. One canonical no-write pipeline invocation exceeded a 30-second wrapper timeout and ended with a downstream broken-pipe error. No source was changed; subsequent canonical runs use a recorded timeout long enough for the complete pipeline.
8. One filtered hanging-indent check incorrectly passed `--root` to a positional-only individual checker; the Python error was hidden by the filtering pipeline while the build/test portion continued. No source was changed by the invalid check; Codex `/root` later detected the missing diagnostic and reran the checker through its actual interface.
9. A direct fixer capability probe incorrectly passed unsupported `--help` to the positional-only hanging-indent fixer, which treated it as a filename and exited. No source was changed.
10. A second direct hanging-indent check repeated the unsupported `--root` assumption in the same diagnostic command. No source was changed. Codex `/root` read both scripts' `main()` functions and switched all individual-rule invocations to positional files or their default repository scan.
11. After the hanging-indent fixer reported all 23 enumerated files token-equivalent, its own no-write checker retained one `operator return block should be normalized` finding in `include/vnm_plot/core/types.h`. The fixer's repeated-pass normalization did not converge on the renderer's required one-column mixed-parenthesis alignment. Codex `/root` applied that exact whitespace-only alignment manually, reran the token comparison, and obtained a green hanging-indent/build/CTest checkpoint.
12. A complete canonical replay after the later expression/hard-limit work found two earlier local-declaration findings reintroduced by ordered downstream formatters in `include/vnm_plot/core/algo.h` and `tests/test_snapshot_caching.cpp`. No unsafe output was retained; the findings were isolated to initialized constant tables.
13. After the local-declaration formatter made those blocks green, the next canonical replay found the assignment-table checker required the opposite spacing on the same two initialized declarations.
14. A combined verification after applying the assignment formatter confirmed the oscillation: assignment layout was green while local-declaration layout was red again. Codex `/root` avoided modifying the dirty external standards checkout and instead reordered only the constant multiplication operands, preserving exact constant values while satisfying both canonical checkers.

The final canonical replay/remediation cycle added sixteen failed executions, bringing the running total to **50**:

15. Three ordered checks exposed downstream formatter interaction in sequence: eight long-condition findings, then four hanging-indent findings after those were wrapped, then the same four long-condition findings after hanging normalization.
16. Two focused parenthesized-return attempts still left two inline equality chains oscillating between the condition and hanging rules.
17. One context-light multi-file `apply_patch` placed opening parentheses in earlier `return` blocks while placing the closing parentheses in the intended blocks. Codex `/root` owned the targeting error, found it through the complete diff before build/commit, and restored every unintended edit with context-rich patches.
18. One focused check after splitting the equality chains still needed hanging normalization; the next two canonical replays exposed a single expression-RHS/condition oscillation in `chrome_renderer.cpp`.
19. Five focused `chrome_renderer.cpp` layout prototypes remained red under one side of that expression-RHS/condition pair. None was committed; each was inspected and replaced by the next narrower form.
20. Two exact-operation-grouping prototypes remained red before the accepted form materialized the existing long-double left product and parenthesized the unchanged right expression. The final canonical pipeline, initialized Release build, and CTest 21/21 then passed.

The independent write-mode investigation added eight failed executions, bringing the running total to **58**:

21. In an isolated candidate clone, the tool reviewer ran three successive aggregate `--write` passes. Each exited zero without producing a valid fixed point: the first changed six files and the next two continued or retained the cross-stage formatter conflict. The no-write verification after each pass failed, accounting for six executions. No token-changing or ternary corruption recurred.
22. Codex `/root` independently reproduced one false-green aggregate write and its immediately red no-write verification, accounting for two executions. The root cause was `style_pipeline.py` running every fixer unconditionally even when that rule's check was already green, then omitting a final all-rule verification.

The repair is isolated in standards commit `ce01c3a` and [Varinomics/varinomics-standards PR #4](https://github.com/Varinomics/varinomics-standards/pull/4). Write mode now prechecks each fixable rule, skips fixers for green rules, verifies confirmed fixes, and performs one final read-only sweep after any fixer ran. It deliberately returns nonzero rather than auto-looping if a later fixer invalidates an earlier rule. Standards tests pass 576/576; on `vnm_plot@2ec8013`, repaired no-write is green and repaired `--write` exits green with zero file changes. The original dirty standards checkout was not modified.

Batch 2 then retained these additional failure classes. They are recorded by event and artifact rather than collapsed into a misleading aggregate because a workflow run, matrix job, worker review, and local command retry are different units:

1. PR validation at `715d6b8` failed on Linux, macOS, and FreeBSD while Windows passed. Linux could not create an OpenGL context under `QT_QPA_PLATFORM=offscreen`; macOS compiled an unconditional `QVulkanInstance` include without Vulkan; FreeBSD omitted `Threads::Threads` from `test_ring_buffer`. Runs `29103078313`, `29103078237`, and `29103078317` remain the failed evidence.
2. All three second-round xhigh reviews returned red. They found build/runtime provenance gaps, a calibration-skipping PASS path, incomplete allocation evidence, counters outside the measured epoch, median-hidden deterministic-zero failures, optional CI evidence packaging, incomplete machine fingerprinting, multi-config executable ambiguity, and incomplete phase-trace validation.
3. An additional owner-supplied review found the unsafe exact-zero median rule, premature manufactured owner approval, diagnostic runs labeled PASS, five-sample static calibration, per-frame trace reopen, producer statistics updated under the writer lock, ambiguous snapshot-byte semantics, obsolete 84-run artifacts, missing plan ancestry, and stale copy-on-snapshot documentation.
4. A first producer-pause diagnostic exposed a deadlock/timeout during epoch-boundary work; the corrected handshake now pauses only between complete logical publications and a 64-series live diagnostic completes with nonzero publications and a terminal `complete` phase.
5. A deliberately undersized one-second canonical-style wrapper timed out and produced a downstream broken-pipe error. No source changed; the canonical rerun with a sufficient timeout passed.
6. Pinned `actionlint` 1.7.7 was too old for the workflow label syntax; 1.7.12 was checksum-verified and passed. A later PowerShell invocation passed a literal `*.yml` glob and failed before linting; the corrected explicit file list passed.
7. Several read-only PowerShell/`rg` inspection wrappers had quoting or parser errors. No product state changed; each was corrected and the successful diagnostic retained. These command failures remain visible in the execution transcript rather than being reclassified as product failures.
8. The first clean rebuild after plan ancestry was fixed repeated the known uninitialized-MSVC-shell failure (`<cmath>` unavailable). The `vcvars64` retry passed. The gate runner now detects or initializes an x64 Visual Studio environment and records its target/toolset/SDK identity, so the documented one-command path no longer depends on the caller's shell state.
9. At `049587c`, Windows and macOS CI passed while Linux run `29107308679` and FreeBSD run `29107308656` reached native QRhi under Xvfb but aborted during `cold.backend_create` with LLVM out-of-memory/`std::bad_alloc`. Linux retained its terminal failure trace and gate manifest. FreeBSD retained only the host-created preflight because the benchmark ran inside a copied VM workspace while packaging ran later on the Ubuntu host. A first NFS-shared correction attempt at `86ad4c0` failed earlier because guest Git correctly rejected the host-owned source and freshly fetched dependencies as unsafe. The workflow retains rsync isolation, packages inside the guest, writes its real test/gate result to a copied-back marker, uploads copied-back evidence, and only then propagates failure on the host. The failure reporter also exposed a missing `sys` import while reporting the original process failure. That import is fixed. The next CI attempt keeps native QRhi and bounds Mesa's hosted software-renderer worker count with `LP_NUM_THREADS=1` to fit the runners' memory limits. This infrastructure hypothesis and the FreeBSD retention correction remain open until that attempt passes.
10. A focused 64-series live review at `fea5a84` proved that the pause duration was excluded but warm-up scheduling debt still survived into the measured epoch: 46 logical samples were published at an aggregate 96,000 samples/s instead of the configured 64,000. The rate schedule now rebases to the current logical publication count while the producer is paused at measurement start, and an inner-loop recheck prevents even one unconditional post-resume publication. The matching 10-frame probe retains zero measured publications, while the fixed 120-frame calibration workload retains 154 logical publications at an aggregate 57,924 samples/s with zero overwrites and zero copied snapshot bytes. The exact numeric calibration margin remains generated evidence for owner review, not a hard-coded threshold.
11. Round 4 at `86ad4c0` ended 5/8 green in PR validation: Windows and macOS passed text OFF/ON and Linux text OFF passed, but Linux text ON run `29109391131` still exhausted LLVM memory during native backend creation and both FreeBSD jobs in run `29109391179` failed Git ownership checks under the attempted NFS workspace. All three xhigh reviewers returned red on those failures. The remediation keeps the measured offscreen buffer at 4x while using a single-sample fallback/context surface, moves renderer environment settings into the complete execution/packaging scope, and verifies them against raw smoke metadata. FreeBSD returns to isolated rsync with in-guest packaging and copied-back result propagation. The gate now retains a synthetic-source terminal FAIL manifest and `failure.txt` even when source identity or preflight collection throws; injected regression tests cover both paths.
12. A softpipe experiment in Linux run `29110165725` eliminated the LLVM out-of-memory failure but correctly failed the pixel oracle: the native OpenGL process returned zero with 4x measured rendering, yet retained zero nonuniform pixels because softpipe tried GLSL 330/150/140/130 and the baked shader asset supplied GLSL 410. The failure bundle retained preflight, invocation, and raw renderer settings and validated every artifact hash. Softpipe is rejected for this smoke; the next attempt keeps the one-sample context and 4x measured render target on the default Mesa renderer with one bounded worker. The gate now requires preflight, invocation, and raw renderer environments to match and tests both the matching and tampered cases.
13. The completed FreeBSD text-ON rsync run at `f07549b` proved that in-guest packaging, copyback, mandatory upload, and host-side status propagation work in order. It also showed that rsync preserves the host checkout owner, so guest Git rejected the source root during gate provenance even though newly fetched guest dependencies were not affected. The workflow now registers only the exact guest checkout returned by `pwd` as a safe directory before configure. It does not use a wildcard; the synthetic-source bootstrap fallback remains failure evidence rather than the normal success path.
14. Default llvmpipe with one worker still exhausted LLVM memory after the fallback/context surface was reduced to one sample, so context sampling alone was not the cause. Review of all 12 current shaders found only desktop GL 3.3-capable attributes, std140 uniform blocks, varyings, derivatives, and texture sampling; no storage-buffer requirement remains. Each shader now retains both GLSL 330 and 410 variants, verified by a resource-level QShader test. Hosted Linux requests a 3.3 Core single-sample context through the ordinary native OpenGL QRhi path and uses softpipe, while the measured offscreen render target remains 4x. The strict nonuniform-pixel oracle, not successful context creation alone, decides whether this compatibility path is acceptable.
15. Linux runs `29111220046` and `29111320276` proved that bounded softpipe now renders real nonuniform frames and produces complete `DIAGNOSTIC_PASS` evidence. FreeBSD run `29111220086` retained a different platform failure: its VM cannot create a softpipe `drisw` screen, so QRhi fails before shader selection. FreeBSD therefore leaves `GALLIUM_DRIVER` unset and uses the available Mesa renderer with `LP_NUM_THREADS=0`, which Mesa documents as disabling llvmpipe rendering threads completely. Native non-Null QRhi, Core 3.3/context1, the 4x measured target, three-way environment provenance, and the strict pixel/trace oracle remain unchanged; the experiment is not presumed green until retained evidence passes.
16. Candidate `a83316e` passed 7/8 hosted jobs. FreeBSD text-ON run `29111950433` verified clean source, copied-back failure evidence, `GALLIUM_DRIVER` unset, and `LP_NUM_THREADS=0`, but still threw `std::bad_alloc` after `cold.backend_create.begin`; disabling render workers therefore did not remove the backend allocation pressure. The next bounded run changes only VM capacity from the action's 6 GiB default to 10 GiB on the documented 16 GiB public Linux host, logs guest physical/user memory, swap, and limits, and records the exact QRhi initialization substep if `std::bad_alloc` recurs. Native QRhi, context1, render4, and the strict pixel oracle remain unchanged.
17. Candidate `836b178` again passed 7/8 hosted jobs. FreeBSD run `29112893803` retained 10,686,750,720 physical bytes, 5,233,643,520 user bytes, free swap, nonrestrictive limits, and an exact `fallback_surface` allocation failure. Qt's documented source shows that its convenience `newFallbackSurface()` first creates a heavy temporary OpenGL context to resolve the format before it constructs the mandatory surface, then destroys that temporary context on return. The benchmark now creates that caller-owned `QOffscreenSurface` directly on the GUI thread with the approved default Core 3.3/sample1 format, requires it to be valid, retains requested and resolved surface formats, and then lets QRhi create the one real native context. Failed global allocations also retain size, alignment, and allocator error without changing failure status. No graphics API, measured target, or pixel oracle is weakened.
18. Candidate `aad57ed` retained the decisive FreeBSD allocation telemetry: QRhi requested a 512-byte aligned allocation with alignment 4, and the benchmark's replacement aligned `operator new` passed 4 directly to `posix_memalign`, which returned `EINVAL` because FreeBSD requires at least pointer alignment. The tracker now raises only the effective C allocator alignment to `sizeof(void*)` when the valid C++ request is smaller, while preserving both requested and effective alignment in failure evidence. Direct successful and failing alignment-4 regression cases cover this platform rule. This is a benchmark instrumentation portability fix; QRhi selection and rendering semantics do not change.
19. Exact candidate `6e815dd` passed all eight hosted jobs and proved the alignment normalization was causal. A subsequent owner-supplied review correctly identified the remaining 10 GiB FreeBSD override, `LP_NUM_THREADS=0`, caller-created fallback surface, and allocation/substep failure telemetry as artifacts of the disproved memory-pressure hypothesis. The active cleanup restores Qt's ordinary `newFallbackSurface()` path and default FreeBSD capacity/renderer settings, removes the failure-only telemetry while retaining the alignment normalization and focused success regression, and requires a fresh ordinary-environment FreeBSD run. The implementation ledger retains the failed hypothesis and evidence; stable architecture documentation does not present it as current design.
20. The same review found that the single phase-trace stream still flushed two records per measured frame. The active trace contract replaces per-frame success records with eleven aggregate success boundaries flushed outside the measured loop, emits a frame-specific boundary only on failure, and continues to enforce exact frame completion through raw metadata and output counters. FetchContent remains on the owner repository's moving `master`; package discovery is unversioned after removal of the residual `0.2.0` `find_package` constraints, and benchmark evidence records the resolved dependency commit.
21. The first full Checkpoint 2.1 gate attempt after exact-head review retained FAIL at `20260710T185509.235869Z-d12f0159`: style, pinned actionlint, configure, build, 29 CTests, and native smoke retention passed, but the environment probe could not open its 267-character text report path on Windows. Python created the deep append-only tree and the benchmark completed its phase trace; the legacy C++ stream path boundary alone failed. The remediation keeps the canonical artifact layout and logical evidence paths unchanged, adapts only Windows native filesystem operations to the extended-length namespace, and adds a report-path regression beyond 300 characters before retrying the full gate.

Preserve all ledgers rather than replacing them with later green results.

| Current state | Owning batch |
|---|---|
| Dependency Windows workflow failed before creating jobs | Recovered and closed by `vnm_msdf_text@b9c216a`: Linux, macOS, FreeBSD, normal Windows, and Windows full export all passed in [run 29090524058](https://github.com/imakris/vnm_msdf_text/actions/runs/29090524058). Dependent `vnm_plot@2d94f01` text-OFF/text-ON CI also passed on all four platforms. Retain the original startup failure as closed evidence. |
| Style baseline | Candidate `vnm_plot@2ec8013` passes the complete no-write pipeline, initialized Release build, CTest 21/21, and eight-platform PR CI. Repaired standards `--write` is a zero-change fixed point. Batch 1B/1C remains open only for final iterative-review closure and merge. |
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
- A review unit is one named, testable commit/work package. It is never broader than one checkpoint and never splits an explicitly atomic migration. Each delegated review unit receives three independent xhigh reviews of one exact source hash, including one explicit architecture/contracts/ownership/missing-primitives audit. Findings return to the implementer; every remediation produces a new exact hash and starts a fresh three-xhigh-reviewer loop. All three must report clean on the same hash, and the owning gate must pass, before merge. Fable is reserved for concrete ambiguity and is not a fourth approval vote.
- The implementation agent is authorized to create local commits, push in-scope review branches, and open PRs. Prefer PR review over direct `master` pushes; do not force-push shared branches or rewrite published history.
- The approved removals of `Data_source_ref`, its raw-reference builders/setters, and ambiguous `Data_source::snapshot()` are intentional breaking API changes. Do not add compatibility aliases or no-op-deleter bridges; provide migration notes and consumer-smoke evidence in the owning batch.

## Responsibilities

These assignments apply unless the owner changes them before a batch starts.

| Batches | Implementation executor | Decision approver / independent reviewer | Gate recorder | Failure-remediation owner |
|---|---|---|---|---|
| Closed 1A–1C and implemented Batch 2 history | Historical executor recorded in commits/ledger | Historical owner/reviewer evidence | Codex `/root` | Historical executor |
| Remaining Checkpoint 2.1 and 2.2 steps | Delegated implementation worker | Three iterative xhigh reviewers; owner for proposal-hash approval | Codex `/root` orchestrator | Implementing worker |
| 3A–3D | Delegated implementation worker per review unit | Three iterative xhigh reviewers, including architecture lens; owner for new product choices | Codex `/root` orchestrator | Implementing worker |
| 4A–4B | Delegated implementation/prototype worker per review unit | Three iterative xhigh reviewers; owner chooses D4 winner from exact evidence hash | Codex `/root` orchestrator | Implementing worker |
| 5 | Delegated implementation worker per review unit | Three iterative xhigh reviewers plus final owner/human approval | Codex `/root` orchestrator | Implementing worker |

### Recorded owner authorization

On 2026-07-10 the repository owner authorized the implementation agent to create local commits, push review branches, and open PRs for the in-scope work. This authorization does not waive batch gates, independent review, branch protection, or the prohibition on destructive history rewriting.

The owner also approved:

- D1–D3 and D5–D10 using the recommended positions below;
- the intentional removal of `Data_source_ref`, the raw-reference API, and ambiguous `Data_source::snapshot()` without compatibility aliases;
- automatic generation of the fixed benchmark scenarios and numeric noise-margin proposal after baseline calibration, followed by owner review of the generated manifest before Checkpoint 2.2.

On 2026-07-11 the owner approved the converged architecture follow-up: new
decisions D11-D15 and the amended D2, D6, D7, D8, and D9 contracts below. These
replace conflicting review proposals for `Published_state`, a
`Frame_orchestrator` component, snapshot capability/batch APIs, registry
incarnations, `geometry_revision`, and the split v1 GPU layout. D4 remains the
only intentionally unresolved architecture decision.

The owner also directed that subsequent implementation be orchestrated rather
than written by the primary agent. Each review unit is delegated to an
implementation worker, then reviewed at one exact hash by three separate xhigh
workers until all three are clean on that hash. Every loop assigns at least one
reviewer an explicit architecture/contracts/ownership/missing-primitives lens.
Findings return to the implementing worker; the remediation hash starts a fresh
three-reviewer loop. Claude Fable is consulted only when the worker/review
evidence exposes genuine ambiguity; it is not a routine fourth vote.

## Decisions required

The owner-approved decisions below are product oracle subject to their executable evidence gates. D4 alone remains unresolved until the Stage 4 A/B experiment and visual review.

| ID | Decision | Approved contract | Status and remaining evidence |
|---|---|---|---|
| D1 | Negative values | Algebraic cumulative stacking; a negative component descends from the previous cumulative value, preserving `h=f+g`. | **Approved 2026-07-10.** Retain visible cancellation/negative verification. |
| D2 | Defined domain | Normalize each member into the closed-interval set defined below: logical-order D8 collapse, nonfinite transformation, interpolation spans, then explicit right hold. Intersect the normalized member sets and compose each closed result span without bridging a removed point or open gap. Candidate-specific mandatory endpoint capacity is `R_A=K*B` endpoint timestamps for A and `R_B=N` grid positions for B; if the normalized intersection exceeds that capacity before optional interior samples, the group/view returns `FAILED(FRAGMENTATION_BUDGET)` with no geometry. | **Amended and approved 2026-07-11.** The normative interval/status table below is the executable oracle; D4 alone chooses the interior sampling algorithm. |
| D3 | Group topology | Optional `stack_group_id` on ordinary series; ascending series ID is order; callers use `apply_series_updates` when topology must change atomically. Individual add/remove may render intermediate membership. | **Approved 2026-07-10.** Caller-disciplined atomicity is the product contract. |
| D4 | Sampling strategy | Unresolved between bounded exact event union and bounded shared grid. Ship exactly one, not both. | **Open.** Stage 4 A/B native-backend and visual evidence; owner selects the winner. |
| D5 | Scalar/style contract | Contribution always uses scalar `get_value`; at least two enabled members; individual `NONE` members contribute invisibly; all-NONE and custom-layer groups fail explicitly. | **Approved 2026-07-10.** Retain the status-table tests. |
| D6 | Busy/failure contract | `structure_key` contains only the non-content request facts defined below and gates wholesale BUSY fallback. `content_key` adds the exact executed observations/derivations and, with D10 eligibility, gates READY reuse. BUSY may publish only the last complete group/view whose `structure_key` matches: every range, indicator, status, sequence, and geometry fact comes from that retained content; no fresh operand is mixed. The result is `STALE_BUSY` with the retained sequences and `content_key`, never READY. | **Amended and approved 2026-07-11.** Retain key-mutation, indefinite-stale observability, and no-generation-mixing verification. |
| D7 | Floating timestamp API | The public `checked_seconds_to_ns(double) -> optional<int64_t>` ingestion helper converts the exact binary64 input value in seconds to the nearest mathematical integral nanosecond, with exact halves away from zero; it range-checks the rounded mathematical result before integer conversion and returns `nullopt` for nonfinite or nonrepresentable input. Typed member-pointer access accepts integral-nanosecond members only; a floating timestamp member pointer is ill-formed at compile time. | **Amended and approved 2026-07-11.** Callers convert once into integral-nanosecond storage. Compile-fail, exact-rational/`nextafter`, UBSan, migration, and no-hot-loop-conversion evidence gate Batch 3B. |
| D8 | Duplicate timestamps and NEAREST tie | Treat both `data_snapshot_t` segments as one logical `at(i)` sequence. Among equal timestamps, greatest logical `i` wins before nonfinite, interpolation, and hold processing. If the winner is nonfinite, apply the nonfinite policy without reconsidering an earlier duplicate. After collapse, an exactly equidistant NEAREST query chooses the greater timestamp and its D8 winner, using exact unsigned nanosecond-distance comparison independent of storage/traversal order and sign. | **Amended and approved 2026-07-11.** Retain ascending/descending/wrapped duplicates, nonfinite winner, signed tie, order-independence, and signed-extreme tests. |
| D9 | Pixel-parity indicators and frame result | Batch 3B defines the canonical evaluator and value result. Batch 3C's single production frame entry creates a snapshot-free immutable frame plan and, after renderer/RHI disposition is known, one immutable frame result. The result copies frame/config/view identities from the plan and contains per-series status, sequence, selected LOD, actual disposition, rendered range, and value-only indicators tagged with cursor request ID. The GUI discards a result whose request ID is not current. No independent indicator snapshot path exists; Batch 3D may optimize copying/publication but may not add a second producer. | **Amended and approved 2026-07-11.** Retain plan/result construction, one-frame latency, disposition, and stale-request rejection tests. |
| D10 | Unchanged-frame reuse | Stable nonzero `current_sequence()` plus stable/non-conservative access semantics authorizes reuse; zero/unstable/conservative inputs rebuild and report why. | **Approved 2026-07-10.** The existing second-frame upload oracle may change after Checkpoint 2.1 retains its baseline. |
| D11 | Widget threading and render-input publication | `Plot_widget` configuration, registry, and public API reads and mutations are Qt-GUI-thread-only; cross-thread callers use queued invocation. `QQuickRhiItemRenderer::synchronize()` copies the complete render input while the GUI thread is blocked, after which item and renderer share no mutable widget state. `Data_source` contents are separate and support concurrent producer, GUI-query, and render access through the source's own synchronization. | **Approved 2026-07-11.** Public thread-contract, synchronization, and migrated Qt tests gate Batch 3A/3D. |
| D12 | Source acquisition domain and hold lifetime | Each `Data_source` object is one acquisition domain. Acquisition-taking methods are `try_snapshot`, `time_range`, `query_time_window`, `query_v_range`, planned `query_sample`, and any future method documented as taking a lock, snapshot, or hold. None may begin on a thread while that thread retains any hold from the same object. Metadata operations (`sample_stride`, LOD count/scale/list, `current_sequence`, `time_order`, direct-query-support flags, and equivalent documented metadata) are non-acquiring and safe while a hold is live. Distinct source objects must not return holds backed by the same non-recursive lock; shared backing must provide independent public-acquisition semantics or is nonconforming. Rejected holds release immediately; selected observations are consumed sequentially, and no final plan/result/cache/record context owns a snapshot. | **Approved 2026-07-11.** Batch 3A owns the public/API/custom-layer prerequisites; Batch 3C alone owns cross-series sharing and production scheduling. No capability enum or acquisition-domain token is introduced. |
| D13 | Registration-slot lifecycle | Renderer-owned per-series state belongs to the registration slot. A prepared frame that observes the series ID absent destroys the slot; re-add after observed absence starts cold. Remove/re-add not observed by a prepared frame is an update. “Explicit reset” means an update through existing descriptor/source/access/custom-layer authorities that changes an already listed `structure_key` fact, such as source object or access/custom semantic revision; it is not a reset API, event, counter, or identity token. No registry incarnation, geometry revision, or series-instance token is introduced. | **Approved 2026-07-11.** Observed/unobserved removal and existing-authority update tests gate Batch 3C/3D. |
| D14 | Source identity | Batch 3A removes `Data_source::identity()`. Source lifetime/object identity are the descriptor `shared_ptr` owner control block plus aliased object pointer; content identity is the stable nonzero per-LOD sequence; access meaning is the access-semantic key/revision; registration lifecycle is D13. No user-defined logical identity authorizes reuse across source objects. In-place backing changes advance sequence; in-place interpretation changes advance semantic revision. | **Approved 2026-07-11.** All current identity cache consumers migrate atomically in Batch 3A. |
| D15 | Stack frame resources and result authority | One frame-wide budget covers main and preview. Admission units are complete `(group,view)` reservations: all MAIN units in ascending `lowest_enabled_series_id`, then all PREVIEW units in the same order. Before acquisition/allocation, each unit charges checked fixed reservations `M` output pairs, `V_limit=2*M` visits, and `H_limit` bytes against shared hard totals `FRAME_M_LIMIT`, `FRAME_V_LIMIT`, and `FRAME_H_LIMIT`; rejection is `FRAME_BUDGET`, emits no geometry, performs no later work, and actual use never refunds or readmits. After retiring non-current stack resources, RHI separately checks exact renderer-owned CPU-vector capacity bytes against `STACK_CPU_BYTES_LIMIT` and allocated QRhi stack-buffer bytes against `STACK_QRHI_BYTES_LIMIT`; cap failure is `RESIDENT_BUDGET`, while an in-capacity QRhi allocation API failure is `RHI_ALLOCATION_FAILED`. Every `(group,view)` publishes exactly one canonical disposition under the table below. | **Approved 2026-07-11.** Main results are preview-invariant. Core admission/status gates precede D4 evidence; RHI/public-result gates precede Stage 5 exposure. |

### Normative key, interval, and disposition expansion

This subsection is part of the decision register, not a second authority.

`structure_key` is the non-content request identity. It contains registration
slots and enabled membership/order; effective main/preview descriptor source
owner control blocks plus aliased object pointers; access-semantic keys and
revisions; interpolation, nonfinite, hold, style-membership, and other
data-affecting descriptor facts; view kind and requested time range;
pre-layout framebuffer width and explicit input/output caps; and the selected
product strategy/version after D4. It excludes current or observed sequences,
selected LODs, source windows/spans, extrema, normalized samples, emitted
events, geometry, and all other content-derived facts.

`content_key` contains the complete `structure_key` plus every observation and
derivation used by the executed result: selected source-local LODs, stable
nonzero observed sequences, source windows and normalized spans, D8 winners,
derived values/extrema, time origin, and emitted event/grid representation.
READY reuse requires an identical `content_key`
and D10-stable sequences/non-conservative semantics. BUSY fallback compares
only `structure_key`, retains the prior complete `content_key` wholesale, and
publishes that retained key and its sequences with `STALE_BUSY`; a structural
change or any FAILED, EMPTY, or budget outcome suppresses the fallback.

The D2 normalization oracle is:

| Input rule | Defined set and endpoint rule |
|---|---|
| Duplicate timestamps | Collapse by D8 in logical `at(i)` order before any other rule. Traversal may be ascending or descending but must produce the same chronological function. |
| Finite LINEAR knots | Two consecutive surviving knots define one closed span `[t_i,t_{i+1}]`; a surviving singleton defines `[t_i,t_i]`. Overlapping/touching closed spans merge only when the function is defined at their shared endpoint. |
| Finite STEP_AFTER knots | The function is right-continuous. It is defined on the same closed span, with the newer value owning `t_{i+1}`; a value-changing boundary retains left-limit then right-value event order so geometry never bridges the jump. |
| `BREAK_SEGMENT` | Remove the nonfinite winning knot and both incident interpolation spans. The removed timestamp is a gap and spans on its two sides never merge through it. |
| `SKIP` | Remove the nonfinite winning knot, make its surviving chronological neighbours adjacent, and apply their interpolation rule. |
| `REPLACE_WITH_ZERO` | Replace the winning sample's scalar value with finite zero before span construction. |
| `REJECT_WINDOW` | Return `FAILED(NONFINITE_REJECTED)` for the complete group/view; emit no partial geometry. |
| Explicit right hold | Only STEP_AFTER with `HOLD_LAST_FORWARD` extends the latest defined value through the requested closed right endpoint. It never crosses a later `BREAK_SEGMENT` marker and never creates left extrapolation. |
| Group domain | Intersect all normalized member sets. Preserve every closed component and shared endpoint, merge only touching components defined at that endpoint, and never bridge a removed point or open gap. An empty intersection is `EMPTY`. |
| Candidate A fragmentation | The mandatory normalized intersection endpoints must fit `R_A=K*B` endpoint timestamps before D-dependent side expansion or optional interior events; otherwise return `FAILED(FRAGMENTATION_BUDGET)`. |
| Candidate B fragmentation | The deterministic grid must reserve every mandatory normalized endpoint within `R_B=N` grid positions before optional interior positions; otherwise return `FAILED(FRAGMENTATION_BUDGET)`. |

Every `(group,view)` has exactly one canonical disposition: `READY`, `EMPTY`,
`STALE_BUSY`, or `FAILED(reason)`. All dispositions except READY and
STALE_BUSY emit no geometry; STALE_BUSY emits only the retained complete
geometry. Per-series observations remain separately visible in the same D9
frame result, but the group never owns an ordered reason set. Evaluation stops
at the first failing phase, performs no speculative later work, and selects one
reason deterministically as follows:

1. Validate registry facts: `STACK_REQUIRES_TWO_MEMBERS`; then, for each member
   in ascending enabled series ID, `STACK_SOURCE_MISSING`,
   `CUSTOM_LAYER_IN_STACK`, `STACK_REQUIRES_TIMESTAMP`, and
   `STACK_REQUIRES_SCALAR_VALUE` in that order; then
   `STACK_GROUP_HAS_NO_VISIBLE_STYLE` and `STACK_BUDGET_TOO_SMALL`.
2. Apply D15 metadata-only frame admission. `FRAME_BUDGET` performs no source
   acquisition and consumes no resident resource. Fixed reservations are never
   refunded based on a later disposition.
3. Acquire members sequentially in ascending enabled series ID under D12. After
   the required observations, a source FAILED maps to
   `FAILED(SOURCE_FAILED)` and outranks EMPTY, which
   outranks BUSY. If neither FAILED nor EMPTY occurred but any source is BUSY,
   discard/ignore every fresh READY observation: an identical retained
   `structure_key` yields wholesale STALE_BUSY, otherwise the result is
   `FAILED(SOURCE_BUSY)` with no geometry.
4. Validate order and selected windows: `UNORDERED_STACK_INPUT`, then
   `LOD_BUDGET_EXCEEDED`.
5. Normalize/intersect under D2: EMPTY, `NONFINITE_REJECTED`, then
   `FRAGMENTATION_BUDGET`; compose with checked double accumulation and report
   `STACK_NUMERIC_OVERFLOW` for a nonfinite/unrepresentable cumulative result.
   GLOBAL/GLOBAL_LOD bounded-query failure is `STACK_GLOBAL_RANGE_UNBOUNDED`.
6. Enforce group output: `STACK_OUTPUT_BUDGET_EXCEEDED` if actual output pairs,
   visits, or bytes exceed `M`, `V_limit`, or `H_limit` respectively.
7. During RHI prepare, retire non-current resources, compute exact owned bytes,
   then report `RESIDENT_BUDGET` or the distinct `RHI_ALLOCATION_FAILED` as D15
   defines. Neither condition leaves partial geometry or an installed partial
   cache entry.

### Architecture-document convergence gate

The decision table above is the only normative architecture register. The plan
body owns execution/evidence steps that cite those decisions; the architecture
review owns findings and rationale. Before Batch 3 public implementation:

- the documentation-only convergence commit contains no production code,
  dormant API, alternate implementation, or new public type;
- no active text prescribes retained frame snapshots, a second range/indicator
  producer, `Data_source::identity()`, registry/geometry/instance revisions, a
  split v1 GPU layout/formula, `Published_state`, `Frame_orchestrator`, a
  snapshot capability hierarchy, batch snapshots, or a separate 3P phase;
- every Batch 3/4/5 action and gate follows D2/D6-D15, and D4 is the sole open
  decision;
- governed Markdown/style, `actionlint`, and `git diff --check` pass, and the
  exact documentation commit receives the delegated three-reviewer closure
  required above before Batch 3 coding.

## Execution automation contract

The implementation should make each batch reproducible through one gate runner and machine-readable manifests rather than a collection of undocumented shell commands. The exact script layout may follow repository conventions established in Batch 2, but it must implement these contracts when the corresponding capability first becomes necessary:

1. **Preflight:** record source commit/diff identity, compiler, CMake, Qt, Python, external standards revision/hash, graphics backend/device/driver, dependency commit, relevant environment, and complete invocation. Missing tools such as `actionlint` are explicit environment-bootstrap failures, not missing product specifications. Bootstrap tools with a recorded version/hash rather than relying on an unversioned global installation.
2. **Gate result:** emit a versioned manifest containing batch/checkpoint, start/end time, command, exit status, phase, inputs, artifact hashes, CI run/job URLs, and recovery relationship. Failed or terminated attempts are append-only; a green rerun never overwrites them.
3. **Artifacts:** write raw local evidence below the active build tree's `gate-artifacts/<batch>/<source-identity>/<timestamp>/` directory and upload the same bundle from CI/PR runs. Do not commit raw timing output to the source tree.
4. **Delivery:** local commits and review-branch pushes are allowed before gates close so CI and reviewers can inspect them. Merge to `master` is the protected event and requires the named batch gate and independent approval.
5. **Delegated implementation and review closure:** the primary agent orchestrates and does not implement subsequent review units. One review unit is one named, testable commit/work package, never broader than a checkpoint and never splitting an explicitly atomic migration. After each unit, run three independent xhigh reviews of its exact source hash; at least one audits architecture, contracts, ownership, and missing primitives. Return concrete amendments to the implementing worker. Every remediation hash starts a fresh three-xhigh-reviewer loop, and all three clean dispositions must name the same hash. Use Claude Fable only to resolve a concrete ambiguity exposed by that loop. Record worker/reviewer identities, hash, findings, amendments, remediation, and clean dispositions.
6. **Native backend contract (owner correction, 2026-07-10):** retained smoke and timing evidence request Qt's native QRhi backend and accept whichever non-Null implementation the platform selects, including D3D, Metal, Vulkan, OpenGL, or a future API. The actual backend, device, and available driver identity are recorded, but no graphics API is itself a product gate. API-specific runs are optional diagnostics only. Null QRhi remains diagnostic and must never masquerade as retained GPU evidence. Raw timing comparisons use one accepted machine/backend/build fingerprint; a fingerprint mismatch invalidates comparison.
7. **Calibration generation:** generate scenario manifests from the fixed plan matrices, dimensions, seeds, warm-up counts, and seven-run protocol. Preserve every raw run. Do not remove outliers automatically; an interrupted or failed run is retained and a replacement run is a separately identified attempt.
8. **Calibration arithmetic:** deterministic counters whose accepted baseline is zero use exact-zero regression rules. For positive metrics, calculate relative median drift from the two calibration sets as specified below. If a median is zero/nonpositive or below declared measurement resolution, report relative margin as unavailable and propose a resolution-based absolute rule instead. The runner must not silently turn unstable calibration into a permissive threshold; it emits `CALIBRATION_REVIEW_REQUIRED` with the observed drift/resolution and proposed rule for owner approval.
9. **A/B evidence:** mechanically reject a D4 candidate that violates correctness, per-group `M`, `V_limit`, or `H_limit`, or D15 frame-admission bounds. For surviving candidates, generate a numerical comparison, deterministic screenshots, pixel differences, and a side-by-side visual bundle. The runner may recommend but never silently select the D4 winner.
10. **Conditional stops:** the Stage 3C source-eligibility failure and any proposed Batch 3E remain owner decisions because they change supported-source scope or public API. Automation must present the failing evidence and the two plan-authorized choices without choosing one.

## Stage 1 — Close the baseline

### Batch 1A — Repair dependency Windows workflow

Execution status recorded 2026-07-10:

- the smallest workflow-only fix landed on dependency `master` as `vnm_msdf_text@b9c216a`;
- the invalid job-level `runner.temp` use was replaced by step-level `RUNNER_TEMP` initialization exported through `GITHUB_ENV`;
- dependency Linux, macOS, FreeBSD, normal Windows, and Windows full export passed at `b9c216a` in run `29090524058`;
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
3. require a lexical C/C++ token-equivalence check for every formatting rewrite before build/test; any intended token change requires an explicit plan amendment and leaves this batch for the named Batch 1C checkpoint;
4. inspect and retain the complete diff, then rerun all preceding rules plus the active rule;
5. run `git diff --check`, the initialized Release build, and CTest 21/21;
6. obtain the historical iterative three-xhigh-reviewer loop required for the batch, feeding findings into the next remediation/review round until all three return green; the then-active Fable quota exclusion is historical evidence only and does not govern later review units;
7. continue until the complete canonical pipeline passes without `--write`.

The canonical check command is:

  ```powershell
  python C:\plms\varinomics\varinomics-standards\tools\style_pipeline.py `
    --root C:\plms\bsd_licensed\vnm_plot
  ```

Preserve the initial standards identity as historical evidence: repository base `f5edc8b` and `style_pipeline.py` SHA-256 `8715313C94D8DCC4257EB3792459BA8D7C759C9CEB6958958B64560FD65CB2F4`. The repaired review identity is commit `ce01c3a` from standards PR #4 and pipeline SHA-256 `38056A555CB3AC8CEE31B633A2F5C68CA6B823885C0EF6B15B95979989498CFC`; its tests pass 576/576, candidate no-write is green, and candidate `--write` is green with zero changes. Repin to the terminal merged standards commit if PR #4 is rebased or amended before merge.

`fix_hanging_indent.py` is quarantined: an isolated replay proved that its operator-return renderer corrupts the `std::string{}` ternary in `tests/test_msdf_lcd_shader_reference.cpp::sample_statement_for_offset()`. Do not run that fixer again until the standards repository has a focused regression test and corrected implementation, or until the affected findings are satisfied by manually reviewed token-equivalent edits. A later green result does not erase the failed aggregate-write execution.

Gate:

- every token-preserving rule checker passes individually; only the explicitly routed Batch 1C naming findings may remain before that checkpoint;
- initialized Release build passes;
- CTest passes 21/21.

Batch 1A is closed and no longer blocks local or merge work. The combined Batch 1B/1C canonical pipeline gate must pass before semantic `vnm_plot` changes are pushed for review.

### Batch 1C — Explicit non-behavioral style-gate token repairs

Batch 1B remains formatting-only. Isolate here only the two checker-enumerated cases that cannot be made green safely without changing tokens:

1. Rewrite `tests/test_msdf_lcd_shader_reference.cpp::sample_statement_for_offset()` from its ternary return to an equivalent empty-expression early return followed by the existing non-empty string construction. This avoids the proven `std::string`/ternary parser collision without changing generated shader text; retain the focused LCD shader-reference assertions.
2. If the ordered naming check still reports them, rename only the private `_pad0`, `_pad1`, and `_pad2` members in `src/core/series_renderer.cpp` to checker-approved names without changing declaration order, field types, initialization values, buffer packing, public headers, or exported symbols.
3. Permit only checker-enumerated adjacent C++ string-literal splits that the long-string rule cannot express without introducing an additional literal token. Require a focused diff proving that concatenated bytes, prefixes, and suffixes are unchanged; the initially known scope is one test message in `tests/test_plot_interaction_item.cpp`.
4. For the two initialized constant blocks where the ordered local-declaration and assignment-table formatters require opposite spacing, permit commutative constant-multiplication operand reordering so each derived expression begins with its already declared base unit. Preserve the exact compile-time values and retain the core/snapshot tests; the exact scope is `choose_snap_ns()` in `include/vnm_plot/core/algo.h` and the main/preview origin regression in `tests/test_snapshot_caching.cpp`.
5. In `tests/test_msdf_lcd_shader_reference.cpp::lcd_enabled_statement()`, permit decomposition of the same generated shader bytes into a `std::string` first clause and short `+`-joined literal clauses when required by the hard line-length gate. The focused shader-reference test must prove the complete generated statement remains byte-identical.
6. Permit outer parentheses around the unchanged boolean return expressions in `same_cache_shape()` and `validate_cached_glyph()` when required to satisfy both the long-condition and hanging-indent checkers.
7. In `qrhi_layer_data_key_t::operator==` and `layout_cache_key_t::operator==`, permit a named prefix boolean followed by the remaining short-circuit chain. Preserve clause order so no field is evaluated earlier than in the original expression; retain cache-key equality and renderer cache tests.
8. In `Chrome_renderer::render_preview_overlay()`, permit materializing the existing long-double left product before its existing division and parenthesizing the unchanged right-side product. Preserve arithmetic operator order and retain renderer/preview geometry tests; benchmark numeric calibration must include this boundary in its proposed noise margins.

Do not use this checkpoint for unrelated cleanup.

Gate:

- retain the exact checker findings before each change and prove that no old padding identifier use remains;
- inspect the complete token-changing diff and preserve the existing uniform-layout `static_assert` coverage;
- initialized Release build and CTest pass 21/21;
- complete canonical style pipeline passes without `--write`;
- repaired canonical pipeline with `--write` passes without changing an already green candidate;
- the same historical iterative three-xhigh-reviewer loop covers Batch 1B and 1C before either is merged; its temporary Fable exclusion is non-governing for subsequent work.

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
- `include/vnm_plot/core/series_window.h`;
- `include/vnm_plot/rhi/qrhi_series_layer.h`;
- examples, benchmark source/window, and focused tests;
- every ordinary and preview source assignment.

Hypothesis: ownership/status changes move lifetime checks to descriptor publication without increasing render-thread snapshot time or producer wait. The full-path boundary is descriptor publication through snapshot consumption and teardown.

Actions:

1. Replace `Data_source_ref` in ordinary and preview descriptors with `shared_ptr<Data_source>`.
2. Remove raw-reference setters/builders without a no-op-deleter bridge.
3. Make `Benchmark_data_source` own/share its `Ring_buffer`; changing only the source pointer type would leave a teardown UAF.
4. Remove ambiguous `Data_source::snapshot()` and `Data_source::identity()`; migrate their production/cache callers atomically to explicit status handling and the D14 owner/object/sequence/semantics facts.
5. Replace free-form snapshot status/payload construction with named invariant-safe READY, EMPTY, BUSY, and FAILED construction; READY validates its snapshot, and no other status carries one.
6. Publish the D11 GUI-thread-only widget contract while preserving the separate internally synchronized `Data_source` concurrency contract.
7. Publish D12's one-object acquisition-domain rule with the exact acquiring and non-acquiring method lists from the register, the same-object no-recursion rule, and the shared-backing conformance rule. No capability enum or domain token is added.
8. Replace the owning custom-layer prepare window with a call-scoped structural view passed by reference: metadata and non-owning spans only, with no snapshot, hold, owner, guard, or other ownership-capable field. Remove raw snapshot access from the record context; layer-prepared/GPU state and copied value metadata remain available for recording.
9. Migrate examples, benchmark ownership, package consumers, custom-layer tests, and record-context assertions in the same public API break; no compatibility shim or dormant alternate context remains.

Gate:

- `rg "Data_source_ref|set_data_source_ref|data_source_ref\("` has no public, production, example, or benchmark hits, and source search proves the `Data_source::identity()` virtual and every cache call/field are absent;
- Ubuntu Clang ASan (`-fsanitize=address`) ordinary/preview lifetime and benchmark teardown pass;
- Ubuntu Clang TSan core/update-remove stress is run with `-fsanitize=thread`; if Qt/QRhi cannot link or run under TSan, record `UNSUPPORTED` with compiler/linker evidence and run the same stress test without sanitizer on all four CI platforms;
- four-status construction tests prove READY cannot coexist with an invalid snapshot;
- an actual shared-lock-backed source proves no recursive same-object call; distinct objects sharing backing either return independently acquired holds or fail the conformance test; metadata/current-sequence/LOD/time-order calls remain safe while a hold exists; rejected attempts release before the next acquisition;
- custom-layer tests prove prepare data is valid only for the call, prepare cannot retain a hold through its public type, and record has no raw snapshot access;
- Qt tests and public documentation enforce GUI-thread-only widget access while source content remains concurrently readable;
- repeat the approved Stage 2 full-path protocol; source wait and latency satisfy its numeric rule.

### Batch 3B — One evaluator and one range producer

Primary files:

- `include/vnm_plot/core/algo.h` and access/query types;
- `src/core/types.cpp`;
- `src/core/auto_range_resolver.cpp`;
- `src/qt/plot_widget.cpp`;
- focused range/interaction tests.

Prerequisite: the recorded owner approvals for D7 and D8 satisfy the evaluator semantic gate. Their named UBSan, performance, duplicate, and nonfinite-winner evidence remains required. D9 renderer-pinned publication belongs to Batch 3C, where the shared frame plan exists.

Hypothesis: one canonical evaluator preserves render semantics while indexed sources eliminate full LOD-0 UI copies/scans; the measurement boundary is evaluator/range request through its canonical value result.

Actions:

1. Add one `resolve_sample_at` producer for ordering, interpolation, duplicate winner, hold, and nonfinite semantics.
2. Preserve GLOBAL value-only/range-only access without timestamps in canonical `Data_source::query_v_range()`.
3. Time-bounded VISIBLE and point queries still require timestamp access.
4. Route auto-adjust and non-pixel-parity point queries through canonical producers; do not add renderer-pinned D9 publication or another snapshot path in this batch.
5. Delete the private range scanner only after value-only/all-time parity is proven.
6. Add public `checked_seconds_to_ns(double) -> optional<int64_t>` exactly as D7 defines. Constrain typed timestamp-member APIs to integral members at compile time; migrate every floating-member example/test/documentation call site to checked ingestion into integral-nanosecond storage.
7. Add `Sample_query_mode::{NEAREST,INTERPOLATE}`; `sample_query_context_t { access, profiler, semantics_key, interpolation, empty_window_behavior, nonfinite_policy }`; `sample_query_request_t { timestamp_ns, mode, context, expected_sequence }`; value-only `sample_query_value_t { resolved_timestamp_ns, value }`; and virtual `Data_source::query_sample(lod, request) -> data_query_result_t<sample_query_value_t>`. `expected_sequence==0` means no expectation. NEAREST returns the chosen collapsed sample's timestamp/value; INTERPOLATE returns the requested timestamp and canonical LINEAR/STEP_AFTER/held value. READY/EMPTY return the exact observed sequence, BUSY/FAILED/UNSUPPORTED preserve their existing meanings, and a caller supplying a nonzero expectation discards/retries any READY/EMPTY sequence mismatch.
8. The default `query_sample` performs exactly one `try_snapshot(lod)`, maps its four statuses, evaluates through `resolve_sample_at`, and releases the hold before returning. A direct indexed override must implement the identical D8/nonfinite/interpolation/hold/status/sequence contract without calling another acquiring method under a live hold. ASCENDING/DESCENDING timestamp brackets and duplicate-run boundaries use direction-aware logarithmic lookup, with any extra nonfinite-policy visits counted; UNORDERED NEAREST is a correct global scan with D8's greater-timestamp exact-distance tie, while default UNORDERED INTERPOLATE is UNSUPPORTED unless a direct override supplies defined semantics.
9. Apply D8 duplicate collapse before nonfinite/interpolation/hold processing in both traversal directions and across split snapshots.
10. Replace duplicate local time constants and `floor_div` with the canonical time-unit implementation; update README query, D7 ingestion, and floating-member migration contracts with the same API change.

Gate:

- existing GLOBAL value-only test remains green;
- unordered `[0,90,10,100]`, query 11 resolves nearest timestamp 10;
- ascending, descending, wrapped-segment duplicates (greatest logical `at(i)` wins), nonfinite-winner behavior, hold, expected-sequence mismatch, direct/default status parity, and all statuses pass;
- equidistant NEAREST cases in ascending, descending, unordered, negative-time, wrapped-duplicate, and signed-extreme inputs choose the greater timestamp using overflow-safe unsigned distance; one-nanosecond-off-midpoint cases choose the strictly nearer timestamp;
- D7 uses an exact-rational oracle derived from the input binary64 bits. Exact halves such as `seconds=+/-1/1024` round away from zero; `std::nextafter` neighbours around both signed rounded-result boundaries establish the representable accepted/rejected inputs instead of pretending that binary64 has a one-nanosecond step there. NaN/infinities return `nullopt`, floating member pointers fail a compile-time detection test, and UBSan observes no invalid conversion or hot-loop floating conversion;
- duplicate scanner is absent only after parity;
- a direct indexed million-sample `query_sample` cursor/auto-adjust override is bounded, returns the same values/statuses/sequences as the default evaluator, and satisfies the Stage 2 numeric regression rule; fallback-source cost remains reported.

### Batch 3C — Shared frame plan and range/render identity

Primary files:

- `src/core/series_window_planner.*`;
- new internal `frame_series_planner.*` only if it immediately replaces current production planning;
- frame range and renderer paths;
- benchmark production-planner entry;
- focused cache/layout tests.

Hypothesis: one production frame entry and snapshot-free immutable plan remove duplicate snapshots/scans and range/render/result disagreement without increasing acquisition cost as retained history grows. The full-path boundary is production planning entry through executed renderer/RHI disposition and immutable frame result.

Actions:

1. Use full pre-layout framebuffer width as the deterministic LOD-budget width; final usable width changes transforms, not selected LOD.
2. **3C1 parity:** add one production frame entry that creates one immutable per-series/per-view plan consumed by VISIBLE range and rendering. The plan owns status, sequence, selected LOD/window/spans, extrema, access/semantic facts, and derived normalized data, but no snapshot or hold.
3. **3C1 result:** after renderer/RHI disposition is known, construct the sole immutable frame result from the exact executed plan rather than accepting independent frame/config/view identities. Publish D9 value-only indicators with cursor request ID; the GUI discards a non-current request. No independent range, indicator, or status producer remains after parity.
4. **3C1 evidence entry:** route the benchmark through the same production frame entry before generating parity evidence. Record the visible full-framebuffer-width LOD change explicitly rather than treating it as an invisible refactor.
5. **3C2 acquisition/reuse:** implement the sole production D12 scheduler. Cache attempted status, sequence, and counters without retaining rejected holds; acquire each shared `(source,LOD)` observation once across series, then consume distinct same-source LODs/main-preview queries sequentially. Finish extrema/staging/custom prepare and destroy every hold before another acquisition on that source and before render-pass recording. Do not cache direct query results without their complete request/context/expected-sequence key.
6. Make internal `Series_view_plan` and renderer caches snapshot-free, own the canonical derived window/plan fields, delete duplicated window/renderer-hold storage, and migrate renderer/tests atomically. Batch 3A supplied the public/custom-layer prerequisites; 3C is the only owner of this production scheduler and internal hold removal.
7. Use the register's `structure_key` for D6 BUSY fallback and `content_key` plus D10 eligibility for READY/geometry reuse. D13 governs observed absence; do not add source identity, registry incarnation, geometry revision, or series-instance tokens.
8. Update README VISIBLE/frame-plan/result/acquisition behavior with the production change.

Gate:

- the production benchmark, VISIBLE range, renderer geometry, D9 indicators, and frame result use the same executed plan, LOD, sequence, window, and actual disposition;
- ten series sharing one source/LOD call `try_snapshot()` once per frame;
- attempted-LOD counters prove rejected holds release before the next acquisition; same-source main/preview with distinct LODs are consumed sequentially without overlap and may truthfully report different sequences;
- custom prepare finishes while its non-owning view is valid, the writer proceeds before render recording, and no plan/result/cache/record context owns a hold;
- delayed cursor results are discarded by request ID, and frame-result identity fields are copied only from the executed plan;
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

Hypothesis: GUI-thread-only revisioned publication eliminates unchanged map/config copying without changing structural geometry identity or frame output. The boundary is Qt synchronize entry through immutable render-input publication.

Actions:

1. Add only a series-map publication revision for Qt copying.
2. In one GUI-thread `apply_series_updates` operation, null entries erase their IDs and add/update/remove commit together; increment the map revision exactly once for the batch.
3. Individual add/remove remains a committed intermediate topology; callers use the batch API when stack membership must change atomically.
4. Copy map/config only when its publication revision changes.
5. Geometry equality continues to use registration slot, owning source owner/object, sequence, access semantics, and plan/window fields; global map revision, color, and label do not invalidate geometry.
6. Conservative callable access without a stable semantics key still invalidates safely.
7. Once D11 enforcement and migrated tests are green, remove widget mutexes made redundant by GUI-thread ownership; do not add a second publication model.
8. Update the README example to use the canonical widget setter for widget-owned fields and document queued cross-thread invocation.

Gate:

- one initial map/config copy, then zero until mutation;
- one mixed add/update/remove call publishes one revision and contains no null map entries;
- GUI-thread enforcement and queued cross-thread invocation tests pass before redundant widget mutexes are removed; source-content concurrency remains independently tested;
- color/label-only stable-semantic update causes no snapshot/range scan/geometry upload;
- conservative callable clone invalidates;
- pixel/command output is unchanged;
- approved Stage 2 synchronization/full-frame metrics satisfy the numeric rule.

## Stage 4 — Settle the stack design with evidence

Stage 4 is private/core evidence work, not public stack exposure. It may
implement both bounded candidates behind benchmark/internal entry points solely
to execute the register's semantics and select D4. It adds no public stack
metadata/API, and the losing candidate is deleted before Stage 5.

### Batch 4A — Core stack contract implementation

The decision register approves D1–D3 and D5–D15 and leaves only D4 open. Batch 4A implements the backend-independent stack semantics and typed results without public RHI/Qt exposure. D3 uses `std::optional<int> stack_group_id` with ascending existing series ID and caller-batched topology; do not add a group descriptor. The implementation includes:

- component order;
- scalar/range projection;
- D8 logical duplicate collapse before nonfinite processing;
- nonfinite and hold behavior;
- the register's complete D2 interval normalization, endpoint inclusion/merge, no-gap-bridging, Candidate A `R_A=K*B` and Candidate B `R_B=N` mandatory-endpoint limits, and `FAILED(FRAGMENTATION_BUDGET)` whole-group result;
- cumulative nonfinite/overflow as a typed whole-group failure with no partial geometry;
- D6 BUSY wholesale reuse, retained sequence/content-key publication, and `STALE_BUSY` disposition;
- singleton, all-NONE, and custom-layer failures;
- GLOBAL/GLOBAL_LOD common-domain behavior;
- cross-source/cross-LOD non-atomic snapshot wording;
- preview and indicator behavior;
- exactly one D9/D15 disposition per `(group,view)` and the register's deterministic validation/admission/acquisition/domain/output precedence; no failure-reason set or speculative later-stage evaluation.

The focused tests are product oracles for the approved contracts. Include
reason-peeling/multi-failure cases, FAILED-over-EMPTY-over-BUSY source cases,
fragmentation plus would-be overflow, and proof that a frame-rejected unit makes
no acquisition. This batch does not select D4, add a public stack API, or add an
alternate result/status channel.

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
- `V_limit=2*M`: fixed per-group/per-view visit reservation and hard limit;
- `V_observed=sum(physical_input_visits)+K*E`: measured composition visits, which must not exceed `V_limit`.

D15 adds a deterministic frame-admission layer above these per-group bounds.
Before either candidate produces decision-grade evidence, the scenario manifest
fixes `FRAME_M_LIMIT`, `FRAME_V_LIMIT`, and `FRAME_H_LIMIT`. Admission charges
the fixed checked reservation `(M,V_limit,H_limit)` for each `(group,view)`
before acquisition/allocation: every MAIN unit in ascending
`lowest_enabled_series_id`, then every PREVIEW unit in the same order. A unit
that cannot fit every remaining total emits no geometry, performs no later work,
and reports `FRAME_BUDGET`. Actual use never refunds or readmits. Candidate A
and B use identical reservations, order, and totals.

Actual prototype upload bytes are `12*K*E + 8*group_span_count + 8*sum_line(span_length+2) + sizeof(actual_prototype_stack_uniform_std140)*U`; upload only real uniform bytes, not alignment padding. Paired equal-timestamp events are stored in left/right vector order, so no GPU event-side stream exists. `H_limit` reservation uses `A*U`. With `K*E<=M`, base is <=12M, group-shared spans <=4M, and padded LINE data <=16M; 40M leaves explicit alignment headroom. Span records must remain group-shared; if formats or ownership change, re-derive `H_limit` before use. Use checked add/multiply/divide for `D*K*K`, `D*K`, `K*E`, `A*U`, `M`, `V_limit`, `V_observed`, every byte count, and every D15 frame total. Arithmetic failure, `V_observed>V_limit`, output pairs greater than `M`, or actual bytes greater than `H_limit` returns `STACK_OUTPUT_BUDGET_EXCEEDED`. Per-group/view observations remain separately reported while fixed reservations are governed by D15 frame admission.

Both prototypes call the same benchmark-local wrapper around the production LOD/window selector with explicit `max_visible_samples`; only composition differs. “Complete input” and `V_observed` count interpolation padding, duplicate-collapse visits, hold samples, and every physical sample examined across attempted and selected LODs. D12 applies: attempted status/sequence/counters are retained, rejected holds release immediately, and selected observations are consumed sequentially with no hold in the final plan or prototype result.

Candidate A — budgeted event union:

- `B=floor(M/(D*K*K))`, require B>=2;
- `R_A=K*B` mandatory normalized-domain endpoint timestamps before D-dependent side expansion;
- shared selector chooses each source-local LOD independently with physical input visits <=B;
- event union exact relative to selected curves;
- STEP pairs are emitted consecutively in left/right vector order at the same timestamp;
- `E<=D*K*B`, `K*E<=M`.

Candidate B — bounded shared grid:

- `N=floor(M/(D*K))`, require N>=2;
- `R_B=N` mandatory normalized-domain grid positions;
- shared selector chooses each source-local LOD independently with physical input visits <=N;
- `E<=D*N`, `K*E<=M`;
- STEP changes use consecutive left/right vector-order events at the sampled grid transition;
- grid approximation and step displacement are explicit.

Before Candidate B implementation/evidence, freeze one deterministic grid
construction covering the effective interval-set domain, origin, endpoint
inclusion, widened timestamp arithmetic, allocation across disjoint spans, and
the `FRAGMENTATION_BUDGET` result when mandatory endpoints alone exceed N. The
same construction defines spike-attenuation and step-displacement evidence; no
run-time grid variant or hidden fallback is permitted.

A/B matrix:

- K={2,8,32}; W={800,3840};
- LINEAR, STEP_AFTER, mixed interpolation;
- phase-shifted grids, narrow spikes, duplicates, gaps, negative cancellation;
- unrelated LOD counts/scales;
- static and live sources;
- native backend, identical Release build/finish/instrumentation.

Record:

- selected LOD/input counts;
- compose CPU time, physical visits, `V_observed`, and `V_limit`;
- output pairs/vertices;
- base/boundary/metadata/uniform/total bytes;
- allocations and memory;
- producer wait and snapshot cost;
- full-frame p50/p95/p99 and GPU-finished time;
- human visual verdict for spikes, steps, gaps, and cancellation.

Decision gate:

- uncapped ordinary series retain identical selected LODs, windows, and pixels on regular/irregular ladders and coarsest-over-budget cases;
- both candidates respect `M`, `V_limit`, and `H_limit` or return `STACK_OUTPUT_BUDGET_EXCEEDED`;
- multi-group scenarios use identical D15 order and `FRAME_M_LIMIT`/`FRAME_V_LIMIT`/`FRAME_H_LIMIT`; rejected `(group,view)` units report `FRAME_BUDGET`, perform no acquisition/allocation, and leave no partial geometry;
- enabling/disabling previews leaves the admitted MAIN set and all MAIN counters unchanged; a constrained replay proves all MAIN units run in ascending `lowest_enabled_series_id` before any equivalently ordered PREVIEW unit;
- both repeat the approved Stage 2 seven-run native-backend protocol with identical workloads and candidate-specific B/N caps;
- choose exactly one strategy using performance plus visual evidence;
- record why the loser was rejected;
- source search proves the losing function, flags, mechanical checks, and docs are absent before Batch 4B closes;
- do not expose C or a strategy switch unless evidence proves applications need it.

## Stage 5 — Land one complete stack feature

### Batch 5 — Atomic end-to-end feature delivery

This is one governed delivery batch even if developed as reviewable local commits. Do not merge or deliver public metadata, an unused planner, or an unused shader separately.

Hypothesis and full-path boundary: the selected bounded strategy renders the owner-approved cumulative function within per-group/view `M`, `V_limit`, and `H_limit`, preserves non-stack behavior, and keeps main/preview producer throughput within the Stage 2 numeric rule. Measure from source publication through frame planning, composition, uploads, GPU finish, preview, and frame-published indicator results.

Scope:

- move/promote the winning benchmark compositor and shared QRhi adapter/shader into the canonical layout/RHI targets, switch the benchmark to the production producer, and delete all benchmark-local winner copies in the same commit;
- `std::optional<int> stack_group_id` on ordinary descriptors, ascending existing series ID as order, and existing `apply_series_updates` for atomic caller-intended topology;
- selected bounded composition planner;
- D15 metadata-only, MAIN-before-PREVIEW frame admission charging fixed `(M,V_limit,H_limit)` reservations against `FRAME_M_LIMIT`, `FRAME_V_LIMIT`, and `FRAME_H_LIMIT` before source acquisition or stack allocation;
- VISIBLE range consumes every bottom/top extremum from the exact same immutable composed plan used by rendering;
- bounded scalar GLOBAL/GLOBAL_LOD range: intersect effective member domains, query scalar min/max at each source-local selected level, accumulate every prefix lower/upper in `double`, cache by domain/levels/sequences/semantics, and report fallback scan work;
- the register's exact `structure_key` and `content_key` in the frame/cache path;
- one group VBO containing contiguous per-member interleaved `{float t_rel,bottom,top}` blocks (`static_assert(sizeof==12)`) and one band shader; do not start with a split shared-time layout;
- AREA bottom/top bands;
- DOTS cumulative knots;
- LINE cumulative boundaries using a separately keyed padded `{t_rel,top}` buffer and four prev/p0/p1/next bindings;
- stack/composition/upload observations first consumed by this feature;
- preview membership/order from ordinary descriptors, while main and preview independently plan their effective sources/access/styles/LODs; a missing/failed preview member suppresses only the complete preview group, never main and never a partial preview;
- the D9/D15 immutable frame result publishes exactly one canonical disposition per `(group,view)`, its mandated fields, cumulative indicators, and per-series observations. `STALE_BUSY` publishes the retained sequences/`content_key`; every `FAILED(reason)` uses the register's exact taxonomy/precedence, including distinct `RESIDENT_BUDGET` and `RHI_ALLOCATION_FAILED`; no expected outcome exists only in logs/counters and no group failure-reason set exists;
- extend the existing `examples/preview_config` executable rather than add another example/QML target;
- focused math/RHI/interaction tests;
- representative benchmark scenarios and documentation.

Cache rules:

- READY base-composition reuse requires an identical `content_key`, stable nonzero sequences, and non-conservative/explicit access semantics;
- unstable/conservative inputs recompute within `M`, `V_limit`, and `H_limit` and record the disable reason;
- BUSY compares only `structure_key`, may reuse only the prior complete match wholesale, publishes its retained sequences/`content_key` as `STALE_BUSY`, and never mixes fresh operands;
- D13 observed absence retires the slot; no source identity/incarnation/geometry/instance token exists and no content-derived fact enters `structure_key`;
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

- define the actual stack std140 uniform record first, static-assert its layout, compute A from its real size/backend alignment, and use checked arithmetic for every per-group/per-view `M`, `V_limit`, and `H_limit` bound;
- observed upload bytes equal `12*K*E + metadata + 8*sum_line(span_length+2) + sizeof(actual_stack_uniform_std140)*U`; aligned reservation uses A*U, and both remain within their checked H_limit/memory bounds;
- RHI retires non-current cached stack resources before comparison, sums every retained CPU vector as `capacity()*sizeof(element)` with checked arithmetic against `STACK_CPU_BYTES_LIMIT`, and sums exact allocated QRhi stack-buffer bytes against `STACK_QRHI_BYTES_LIMIT`. Exceeding either cap reports `RESIDENT_BUDGET`; an allocation API failure despite remaining configured capacity reports distinct `RHI_ALLOCATION_FAILED`. Neither leaves partial geometry/cache state or claims a bound on opaque driver physical residency;
- fixed reservations never exceed `FRAME_M_LIMIT`, `FRAME_V_LIMIT`, or `FRAME_H_LIMIT`; all MAIN units precede all PREVIEW units, each class orders by ascending `lowest_enabled_series_id`, previews cannot change MAIN admission, and rejected units emit no geometry with no refund/readmission from actual use;
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
3. shader bake/resource lifecycle passes through Qt's native non-Null backend on every supported platform; API-specific diagnostic coverage is optional;
4. dependency and `vnm_plot` Linux/Windows/macOS/FreeBSD workflows pass;
5. clean no-sibling configure/install/consumer smoke passes while fetching dependency `master`;
6. benchmark metadata names actual backend and resolved dependency commit;
7. deterministic and end-to-end performance gates pass;
8. every completed review unit has three clean xhigh reviews of the same exact hash, including an architecture/contracts/ownership/missing-primitives review; every remediation hash restarted that loop, and owner/human approval closes external product and visual decisions;
9. all observed failures, including recovered ones, remain in the handoff;
10. worktree contains only the intended batch changes.

## Recommended immediate next action

Batch 1A is closed and the Batch 1B style baseline is the base of Batch 2 delivery PR #18. Checkpoint 2.1 remediation includes the plan-ancestry merge, hardened retained evidence through `fea5a84`, the measured-epoch rate rebase, fail-closed gate bootstrap, three-way hosted-renderer provenance, in-guest FreeBSD packaging with copied-back status/evidence, removal of false-hypothesis renderer/VM workarounds, aggregate durable phase tracing, unversioned dependency discovery, and Windows extended-length artifact I/O. The remediation is preserved locally at `26a4cc6`; its Release build, CTest 30/30, 41 Python helper tests, `actionlint` 1.7.12, canonical Varinomics style, and diff checks pass. The D11-D15 convergence record gates future Batch 3 public work but does not reopen the fixed Checkpoint 2.1 protocol. Next, obtain clean cross-platform CI for the exact committed candidate and run a fresh three-xhigh-reviewer loop naming that exact hash; any remediation hash restarts the loop. Then run the fixed 108-execution calibration plus its separate environment probe and present the exact retained proposal SHA-256 to the owner. Checkpoint PASS is impossible until the owner explicitly approves that exact proposal hash. D4 remains open for private Stage 4 evidence and does not block completing Checkpoint 2.1.
