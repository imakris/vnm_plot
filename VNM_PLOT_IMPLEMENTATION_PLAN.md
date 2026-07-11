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

Public stack exposure does not begin until Stages 2–4 pass and the owner selects D4 from the complete evidence. Private/core Stage 4 implementations of both candidates are explicitly allowed only to produce that evidence; they expose no public stack API, and the loser is deleted. The separately authorized dependency Windows repair is historical and closed at `vnm_msdf_text@b9c216a`.

The initial 2026-07-10 Fable quota exclusion is historical and non-governing.
Subsequent work uses delegated execution: Codex `/root` orchestrates, a worker
implements each source unit or runs each evidence-only unit, and three
independent xhigh reviewers review the exact source identity or evidence
identity until all three are clean.
At least one reviewer in every loop audits architecture, contracts, ownership,
and missing primitives.
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
| Style baseline | Batch 1B/1C closed at `vnm_plot@2ec8013`: the complete no-write pipeline, initialized Release build, CTest 21/21, and eight-platform PR CI passed, and repaired standards `--write` was a zero-change fixed point. |
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
- A controlling review unit is one numbered source action, one explicitly
  predeclared same-change cluster, or one named evidence-only unit. A cluster is
  allowed only when this plan records its action IDs and why an intermediate
  hash would be uncompilable, unsafe, or contractually false before delegation.
  A whole batch/checkpoint is never implicitly one unit, no two named units
  combine, and an explicitly atomic cluster never splits. A source unit's
  identity is its exact source hash. An evidence-only unit's identity is the
  tuple `(exact source hash, evidence-manifest SHA-256)` and it makes no semantic
  code change. Any source or evidence-manifest change starts all three reviews
  again. Each unit receives three independent xhigh reviews of that exact
  identity, including one architecture/contracts/ownership/missing-primitives
  audit. Findings return to its implementer; remediation remains in that unit.
  All three must report clean on the same identity and the unit gate must pass
  before merge. Fable is ambiguity-only, not a fourth vote.
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

### Named review-unit map

The names below are sequencing containers. A source review unit is one numbered
action written as `RU-name/A<n>` unless an atomic cluster is listed; a named
evidence-only action uses the exact identity stated in its row. No container may
be reviewed or merged as one undifferentiated batch.

| Name | Scope and controlling split |
|---|---|
| `RU-2.1-remediation` | Remaining Checkpoint 2.1 remediation actions, one action per unit. |
| `RU-2.1-exact-head` | Evidence-only A1 exact-head closure, identified by `(exact source hash,CI/gate-manifest SHA-256)`; any red causal correction becomes a separately numbered `RU-2.1-remediation/A<n>`, receives source review, and is followed by a fresh A1. |
| `RU-2.1-calibration` | Evidence-only fixed 108-execution capture/proposal generation, identified by `(exact source hash, proposal-manifest SHA-256)`; owner proposal-hash approval is a separate non-delegated decision. |
| `RU-2.2` | Numbered Checkpoint 2.2 source actions. Actions 2–4 plus action 6 unconditionally form atomic `CACHE_KEY_MIGRATION`: owner identity, upload key, content revision, and LINE derivative change together so reuse remains ABA-safe. A1, A5, A7, and A8 remain individual source units. |
| `RU-2.2-evidence` | Evidence-only A1 after every RU-2.2 source unit is clean, identified by `(exact source hash,before/after-manifest SHA-256)`; it owns the approved-noise comparison and retained raw artifacts and makes no semantic source change. |
| `RU-3A` | Ordered, non-overlapping units: after P-S1 ratification, `SOURCE_API_BREAK` comprises actions 1–5 and 10 because intermediate public headers/ordinary consumers do not compile; action 6 is independent; `D12_HOLD_BREAK` then comprises actions 7–9 and 11 because publishing D12 or structural custom borrowing while a custom/current renderer path retains a hold would be false. |
| `RU-3B` | After P-Q1 and P-D7 ratification, `POINT_QUERY_CONTRACT` comprises actions 1, 7–9, 11, and 12 because evaluator/API/default implementation/point consumers/docs must expose one contract. `D7_INGESTION` comprises actions 6 and 13 so public ingestion/member behavior and normative docs cannot diverge. Actions 2–5 and 10 remain individual units. |
| `RU-3C1` | Parity actions 2–4 form `FRAME_TRUTH_PARITY`; separate producers may be deleted only in the same change that installs their production replacement/evidence entry. |
| `RU-3C2` | Scheduler/reuse/docs actions 5–8 form `SHARED_ACQUISITION_SCHEDULER`; sharing, keys, cache migration, and normative production behavior move together so there is exactly one documented scheduler. |
| `RU-3D` | Numbered Qt publication actions, one action per unit unless a future pre-delegation amendment names a necessary cluster. |
| `RU-contract-ratification-record` | After the owner decision, one documentation source unit records each proposal's exact ratified/rejected text and statuses and numbers the now-authorized RU-4A actions; it receives the ordinary three-reviewer exact-hash loop. Current RU-4A scope bullets are not executable review actions. |
| `RU-4A` | Numbered private core-contract actions created only by the reviewed ratification record after P-D2/P-R1/P-D15 owner decisions. |
| `RU-4B-common` | Numbered common selector/adapter/scenario implementation action, independently reviewed. |
| `RU-4B-A` | Numbered private Candidate A implementation action, independently reviewed. |
| `RU-4B-B` | Numbered private Candidate B implementation action, independently reviewed. |
| `RU-4B-evidence` | Evidence-only comparison, identified by `(exact candidate source hash, evidence-manifest SHA-256)` after the common/A/B source units are independently clean; it changes no semantic code and a clean review does not approve D4. |
| owner D4 | Non-delegated owner reviews the exact RU-4B-evidence identity, records the comparative visual verdict, and selects D4 in one decision; workers/evidence reviewers store hashes and technical findings but no human verdict. |
| `RU-4B-cleanup` | Numbered action deletes the losing candidate/config/tests and binds the winner to the decision; it begins only after owner D4. |
| `RU-5-production` | Three internal, immediately consumed source units: A1 winner promotion/private-copy deletion/benchmark production entry; A2 internal frame/range/cache producer consumed by benchmark/internal entry; A3 internal RHI consumed by that entry. |
| `RU-5-public` | `STACK_PUBLIC_ACTIVATION` atomically clusters A1 metadata/registry reachability and A2 Qt/result/preview plus normative public API docs/package-consumer smoke; required public reachability/contract/docs never land separately. A3 is independent and contains only non-contractual example/tutorial expansion and additional package evidence. |
| `RU-5-final-evidence` | Three evidence-only units: A1 correctness/platform, A2 performance/visual generation and technical review, A3 release audit/approval-binding validation. Each uses `(exact source hash,evidence-manifest SHA-256)`, makes no semantic code change, and cannot manufacture human approval. |
| human visual approval | Nondelegated approval occurs only after clean A2 and is bound to A2's exact `(source hash,evidence-manifest SHA-256)` identity; workers/reviewers cannot create or substitute it. |

### Common executable unit gates

Every source unit or declared source cluster runs, at minimum, `git diff
--check`, the canonical no-write style pipeline, initialized Release configure
and build, full CTest, every applicable Python helper suite, and every focused
oracle allocated to that unit below. A behavior change without a focused oracle
fails the unit. Public API units additionally run clean install/package consumer
smoke. Ownership/concurrency units run their named ASan/TSan gates, or retain a
toolchain/link/runtime `UNSUPPORTED` record plus the required unsanitized
four-platform stress. RHI units run native non-Null lifecycle and pixel gates.
These are executable minimums, not evidence borrowed from a later unit.

Every evidence-only unit runs the versioned evidence runner and validates the
manifest schema, source/environment/dependency/executable provenance, complete
artifact hashes, terminal status, and retained failure/recovery relationships.
It proves zero semantic source change from its named source hash. A runner,
schema, artifact, provenance, or terminal-validation defect fails that evidence
unit; remediation occurs in a separately named source unit before a fresh
evidence identity is reviewed.

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
incarnations, `geometry_revision`, and the split v1 GPU layout. That approval
does not ratify later executable refinements merely because an implementation
reviewer or Fable proposed them. The explicitly pending refinements below remain
proposals until the owner approves their exact text.

The owner also directed that subsequent implementation be orchestrated rather
than written by the primary agent. Each source unit is delegated to an
implementation worker; each evidence-only unit has a named evidence runner.
Three separate xhigh workers review the exact source-hash or
source-hash/manifest identity until all three are clean on that identity. Every
loop assigns at least one reviewer an explicit
architecture/contracts/ownership/missing-primitives lens. Findings return to
the source implementer or evidence runner, and any source/manifest change starts
a fresh three-reviewer loop. Claude Fable is consulted only when the
worker/review evidence exposes genuine ambiguity; it is not a routine fourth
vote.

## Decisions required

The approved portions below are product oracle subject to their executable
evidence gates. D4 and the separately labelled executable refinements remain
open. A review, worker, or implementation commit cannot manufacture owner
ratification.

| ID | Decision | Approved contract | Status and remaining evidence |
|---|---|---|---|
| D1 | Negative values | Algebraic cumulative stacking; a negative component descends from the previous cumulative value, preserving `h=f+g`. | **Approved 2026-07-10.** Retain visible cancellation/negative verification. |
| D2 | Defined domain | Process each member in logical source order; collapse equal timestamps under D8; apply the nonfinite policy; construct its piecewise function and defined interval set; apply explicit right hold; intersect member interval sets; compose each resulting interval without bridging gaps. If mandatory interval endpoints exceed the bounded candidate representation, the complete group/view fails with no geometry. | **Amended and approved 2026-07-11.** Exact interval normalization and candidate capacities are pending proposal P-D2. |
| D3 | Group topology | Optional `stack_group_id` on ordinary series; ascending series ID is order; callers use `apply_series_updates` when topology must change atomically. Individual add/remove may render intermediate membership. | **Approved 2026-07-10.** Caller-disciplined atomicity is the product contract. |
| D4 | Sampling strategy | Unresolved between bounded exact event union and bounded shared grid. Ship exactly one, not both. | **Open.** Stage 4 A/B native-backend and visual evidence; owner selects the winner. |
| D5 | Scalar/style contract | Contribution always uses scalar `get_value`; at least two enabled members; individual `NONE` members contribute invisibly; all-NONE and custom-layer groups fail explicitly. | **Approved 2026-07-10.** Retain the status-table tests. |
| D6 | Busy/failure contract | BUSY may reuse only the last complete group/view for an identical non-content structural request. All members, range, indicators, statuses, and private rendered geometry come from that retained complete content; fresh and stale generations never mix. The public value-only result reports `STALE_BUSY` and retained sequences/content identity, never READY, but exposes no geometry. READY, EMPTY, FAILED, a budget failure, or structural change replaces or suppresses stale fallback. | **Amended and approved 2026-07-11.** Exact `structure_key`/`content_key` tuples are pending proposal P-D6. |
| D7 | Floating timestamp API | The public `checked_seconds_to_ns(double) -> optional<int64_t>` ingestion helper converts the exact binary64 input value in seconds to the nearest mathematical integral nanosecond, with exact halves away from zero; it range-checks the rounded mathematical result before integer conversion and returns `nullopt` for nonfinite or nonrepresentable input. Typed member-pointer access accepts integral-nanosecond members only; a floating timestamp member pointer is ill-formed at compile time. | **Amended and approved 2026-07-11.** Callers convert once into integral-nanosecond storage. Compile-fail, exact-rational/`nextafter`, UBSan, migration, and no-hot-loop-conversion evidence gate Batch 3B. |
| D8 | Duplicate timestamps | Treat both `data_snapshot_t` segments as one logical `at(i)` sequence. Among equal timestamps, greatest logical `i` wins before nonfinite, interpolation, and hold processing. If the winner is nonfinite, apply the nonfinite policy without reconsidering an earlier duplicate. | **Amended and approved 2026-07-11.** The exact equidistant NEAREST rule is pending proposal P-Q1. |
| D9 | Pixel-parity indicators and frame result | Batch 3B defines the canonical evaluator and value result. Batch 3C's single production frame entry creates a snapshot-free immutable ordinary frame plan and, after renderer/RHI disposition is known, one immutable ordinary frame result. The result copies frame/config/view identities from the plan and contains per-series status, sequence, selected LOD, actual disposition, rendered range, and value-only indicators tagged with cursor request ID. The GUI discards a result whose request ID is not current. No independent indicator snapshot path exists; Batch 3D may optimize copying/publication but may not add a second producer. This ordinary D9 result is complete without P-R1/P-D15 or any stack section; Stage 5 later adds P-R1's stack extension without changing the ordinary contract. | **Amended and approved 2026-07-11.** Retain plan/result construction, one-frame latency, disposition, stale-request rejection, and ordinary-with-stack-fault independence tests. |
| D10 | Unchanged-frame reuse | Stable nonzero `current_sequence()` plus stable/non-conservative access semantics authorizes reuse; zero/unstable/conservative inputs rebuild and report why. | **Approved 2026-07-10.** The existing second-frame upload oracle may change after Checkpoint 2.1 retains its baseline. |
| D11 | Widget threading and render-input publication | `Plot_widget` configuration, registry, and public API reads and mutations are Qt-GUI-thread-only; cross-thread callers use queued invocation. `QQuickRhiItemRenderer::synchronize()` copies the complete render input while the GUI thread is blocked, after which item and renderer share no mutable widget state. `Data_source` contents are separate and support concurrent producer, GUI-query, and render access through the source's own synchronization. | **Approved 2026-07-11.** Public thread-contract, synchronization, and migrated Qt tests gate Batch 3A/3D. |
| D12 | Source acquisition domain and hold lifetime | Each `Data_source` object is one acquisition domain. Acquisition-taking methods are `try_snapshot`, `time_range`, `query_time_window`, `query_v_range`, planned `query_sample`, and any future method documented as taking a lock, snapshot, or hold. None may begin on a thread while that thread retains any hold from the same object. Metadata operations (`sample_stride`, LOD count/scale/list, `current_sequence`, `time_order`, direct-query-support flags, and equivalent documented metadata) are non-acquiring and safe while a hold is live. Distinct source objects must not return holds backed by the same non-recursive lock; shared backing must provide independent public-acquisition semantics or is nonconforming. Rejected holds release immediately; selected observations are consumed sequentially, and no surviving plan/result/cache/record context owns a snapshot. | **Approved 2026-07-11.** RU-3A publishes the contract and atomically makes every current per-series/custom path hold-free; RU-3C2 alone replaces independent acquisitions with cross-series/shared-key scheduling. No duplicate scheduler, capability enum, or domain token is introduced. |
| D13 | Registration-slot lifecycle | Renderer-owned per-series state belongs to the registration slot. A prepared frame that observes the series ID absent destroys the slot; re-add after observed absence starts cold. Remove/re-add not observed by a prepared frame is an update. “Explicit reset” means an update through existing descriptor/source/access/custom-layer authorities that changes an already listed `structure_key` fact, such as source object or access/custom semantic revision; it is not a reset API, event, counter, or identity token. No registry incarnation, geometry revision, or series-instance token is introduced. | **Approved 2026-07-11.** Observed/unobserved removal and existing-authority update tests gate Batch 3C/3D. |
| D14 | Source identity | Batch 3A removes `Data_source::identity()`. Source lifetime/object identity are the descriptor `shared_ptr` owner control block plus aliased object pointer; content identity is the stable nonzero per-LOD sequence; access meaning is the access-semantic key/revision; registration lifecycle is D13. No user-defined logical identity authorizes reuse across source objects. In-place backing changes advance sequence; in-place interpretation changes advance semantic revision. | **Approved 2026-07-11.** All current identity cache consumers migrate atomically in Batch 3A. |
| D15 | Stack frame resources and result authority | Stack composition is all-or-nothing per group/view. A deterministic frame-wide planner admits complete groups against hard total work, visit, and upload budgets; rejection emits no geometry and reports a frame-budget failure. RHI separately caps renderer-owned live CPU and QRhi stack-buffer allocation capacity, retires non-current cached resources before prepare, and reports allocation-cap failure. Every expected failure, suppression, and stale reuse appears in the immutable frame result rather than only logs/counters. | **Approved 2026-07-11.** Exact admission order/reservations, result precedence, and resident/allocation details are pending P-D15 and P-R1. |

### Proposed executable refinements pending owner ratification

This subsection records precise proposals in the sole decision register so the
owner can ratify them in one explicit action. It is not product oracle until
that action occurs:

- **P-D6:** exact compact `structure_key`/`content_key` membership;
- **P-D2:** exact interval normalization and Candidate A/B fragmentation
  capacities;
- **P-S1:** top-level snapshot-result sequence and invariant-safe factories;
- **P-D7:** exact integral timestamp-member admissibility trait;
- **P-Q1:** the complete public point-query API, status, search, interpolation,
  ordering, and performance contract;
- **P-R1:** self-describing ordered stack results, factory-enforced
  disposition/payload invariants, table-owned public views, deterministic
  precedence, and per-series observation/stale-presentation vocabulary;
- **P-D15:** MAIN-before-PREVIEW immutable-slice admission, `U_limit`
  accounting, exact resident byte caps, and resource-allocation failure
  distinction.

RU-3A `SOURCE_API_BREAK` cannot begin until P-S1 is ratified. RU-3B cannot
begin until both P-Q1 and P-D7 are ratified. RU-3C2 cannot begin until P-D6 is
ratified. RU-4A, RU-4B-common, RU-4B-A, RU-4B-B, RU-4B-evidence,
RU-4B-cleanup, and stack-related Stage 5 units cannot begin until P-D2, P-R1,
and P-D15 are ratified. The owner decision is followed by
`RU-contract-ratification-record`, which records exact ratified/rejected text
and statuses and, for the stack proposals, numbers the authorized RU-4A actions.
No affected implementation RU begins until that record is clean at one exact
documentation hash. Ratification does not select D4.

#### P-S1 — Snapshot-result sequence and factories

The four-status snapshot result owns a top-level `sequence` independent of its
optional payload and permits construction only through named factories:

- `ready(snapshot)` requires a valid payload and copies
  `snapshot.sequence` into the top-level sequence;
- `empty(sequence)` has no payload. A nonzero sequence is the exact observed
  empty revision; zero explicitly means that an exact empty revision is
  unavailable or unstable and never authorizes reuse;
- `busy()` and `failed()` have no payload and set the top-level sequence to
  zero.

No caller reconstructs an observation sequence with a post-hoc
`current_sequence()` read. READY payload/top-level mismatch, a payload on any
other status, or a claimed BUSY/FAILED sequence is unrepresentable through the
public factories.

The pending snapshot result has private constructors and private storage, no
public fields or mutators, and read-only `status()`, `sequence()`, and
`snapshot_if_ready()` accessors. `snapshot_if_ready()` returns a non-owning
pointer only for READY and null otherwise; contextual `explicit operator bool()`
is true exactly for READY. These observers cannot manufacture or mutate a
status/payload/sequence combination.

#### P-D7 — Integral timestamp-member admissibility

For `M=remove_reference_t<T>` and `U=remove_cv_t<M>`, a timestamp member type is
admissible exactly when `!is_volatile_v<M>`, `is_integral_v<U>`,
`!is_same_v<U,bool>`, and its complete value range fits `int64_t`: for signed `U`,
`numeric_limits<U>::digits<=numeric_limits<int64_t>::digits`; for unsigned `U`,
the same digits comparison requires its maximum to fit the positive signed
range. Const qualification is accepted; volatile qualification is rejected.
This admits signed `int64_t` and fitting smaller signed/unsigned types, and
rejects bool, floating types, `uint64_t`, and wider integral types.

#### P-Q1 — Complete point-query contract

The public types are
`Sample_query_mode::{NEAREST,INTERPOLATE}`;
`sample_query_context_t { access, profiler, semantics_key, interpolation,
empty_window_behavior, nonfinite_policy }`;
`sample_query_request_t { timestamp_ns, mode, context, expected_sequence }`;
value-only `sample_query_value_t { resolved_timestamp_ns, value }`; and
an invariant-safe `sample_query_result_t`. This result is not the generic
mutable `data_query_result<T>` and is constructible only by named factories:

- `ready(value,sequence)` carries exactly one value and the exact observed
  sequence;
- `empty(sequence=0)` carries no value; nonzero is the exact observed empty
  revision and zero means unavailable/unstable;
- `busy()` and `unsupported()` carry no value or reason and sequence zero;
- `failed(Sample_query_failure_reason)` produces observable `FAILED(reason)`,
  carries no value, and has sequence zero; the closed reason enum is exactly
  `Sample_query_failure_reason::SOURCE_FAILED`,
  `Sample_query_failure_reason::INVALID_SOURCE_RESULT`, and
  `Sample_query_failure_reason::NONFINITE_REJECTED`.

The result uses `Data_query_status` for `status()`. It has private constructors
and storage, no public fields or mutators, and read-only `status()`, `sequence()`,
`value_if_ready()`, and `failure_reason_if_failed()` accessors.
`value_if_ready()` and `failure_reason_if_failed()` return non-owning pointers
only in their matching disposition and null otherwise; contextual
`explicit operator bool()` is true exactly for READY. Invalid
status/value/reason/sequence combinations are unrepresentable.
`Data_source::query_sample(lod,request)` returns `sample_query_result_t`.
`expected_sequence==0` requests no comparison. For a nonzero expectation, a
READY/EMPTY result still reports its exact observed sequence, and the canonical
caller discards that invocation's result rather than publishing it. It performs
no synchronous retry or spin; only a later independently scheduled request may
retry.

The default implementation performs exactly one `try_snapshot(lod)`, maps that
result's top-level status/sequence without a post-hoc `current_sequence()` read,
evaluates within the acquired snapshot, and releases before return. Snapshot
FAILED maps to `FAILED(SOURCE_FAILED)`; any malformed/invariant-breaking source
result maps to `FAILED(INVALID_SOURCE_RESULT)`; point REJECT maps to
`FAILED(NONFINITE_REJECTED)`, each through the invariant-safe failed factory. A direct override preserves the same factory
invariants, status/sequence/expectation contract, and never nests another
acquisition on the same source.

D8 duplicate collapse precedes point nonfinite handling. Semantic support and
physical visits are distinct. NEAREST uses timestamps and D8 collapse to prove
the minimum-distance timestamp, choosing the greater timestamp on an exact
overflow-safe unsigned-distance tie; its semantic support is only that chosen
timestamp's D8 winner even if physical search visited unrelated history.
`REJECT_WINDOW` fails with `FAILED(NONFINITE_REJECTED)` only when that chosen
winner is nonfinite. `REPLACE_WITH_ZERO` returns zero when that winner is
nonfinite. `SKIP` removes it and continues outward, counting every visit.
For NEAREST, `BREAK_SEGMENT` likewise removes a nonfinite candidate and
continues outward; its gap semantics apply only to interpolation, so finite
knots on either side remain ordinary NEAREST candidates.

INTERPOLATE semantic support is exactly the D8 winner at an exact timestamp,
the required pair of bracketing winners, or the required held endpoint winner.
REJECT fails only when a required support winner is nonfinite. SKIP may continue
outward and reconnect surviving brackets while counting visits. BREAK never
bridges its removed knot and returns EMPTY when the request lies in that gap.
INTERPOLATE has no left extrapolation and, for both LINEAR and STEP_AFTER, uses
constant `HOLD_LAST_FORWARD` through the requested right endpoint;
`DRAW_NOTHING` does not extend the domain.

ASCENDING and DESCENDING use direction-aware lookup. UNKNOWN is classified
only within the acquired snapshot; RU-3C2 may reuse that classification only
under P-D6's weak-owner/alias source identity, stable nonzero observed sequence,
and access semantics. An
effectively UNORDERED default source performs a counted global scan for
NEAREST, but its semantic support remains only the chosen D8 winner. Its
default INTERPOLATE returns UNSUPPORTED and never sorts. A direct
override may define UNORDERED interpolation only while preserving every
status, sequence, duplicate, nonfinite, hold, expected-sequence, and
no-nested-acquisition rule above.

The `query_sample` point/cursor gate is distinct from the `query_v_range`
range/auto-adjust gate; neither performance result substitutes for the other.

#### P-D6 — Compact reuse identities

`structure_key` is the non-content request identity. It contains registration
slots and enabled membership/order; effective main/preview descriptor source
identities; access-semantic keys and
revisions; interpolation, nonfinite, hold, style-membership, and other
data-affecting descriptor facts; view kind and requested time range;
pre-layout framebuffer width and explicit input/output caps; and the selected
product strategy/version after D4. It excludes current or observed sequences,
selected LODs, source windows/spans, extrema, normalized samples, emitted
events, geometry, and all other content-derived facts.

Each inherited source identity stores a non-owning
`weak_ptr<Data_source>` owner handle plus the aliased `Data_source*`. Two source
identities compare equal if and only if neither `owner_less(a.owner,b.owner)` nor
`owner_less(b.owner,a.owner)` is true, and then the alias pointers are equal.
The key never
serializes or hashes a control-block address and never adds strong lifetime
extension. Expiry remains observable through the weak handle and invalidates
use; address equality without owner equivalence never matches.

`content_key` is a compact equality key and nothing else. It contains the
complete `structure_key` and, for every selected source observation in
deterministic member/LOD order: selected source-local LOD; atomically observed
sequence including zero; selected logical source window `(first,count)`; the
synthetic-right-hold `(present,endpoint_timestamp)`. The remaining scalar fields
are the time origin and exact planner/data-format version numbers. It excludes
the snapshot's physical first/second-segment split and all normalized/drawable
span boundaries or arrays: segmentation is representation, while normalized
spans are produced artifacts. It also contains no samples, D8 winners or
values, arrays, extrema, events, geometry, CPU staging, or GPU objects.
The tuple adds no payload/resource owners; its inherited source identities are
the non-owning weak identities above.

`structure_key` and `content_key` are renderer-private equality state. They are
never exposed, serialized, copied into, or accepted from the public frame
result/API. The private retained cache alone keeps `content_key` for READY reuse
and BUSY eligibility. P-R1's public group/view/member IDs are structural labels
without reuse authority. Public content provenance is only `content_frame_id`
plus exact per-series presented sequences; no weak owner handle, alias pointer,
structure key, or content key crosses that boundary.

READY reuse requires an identical `content_key`, stable nonzero observed
sequences, and D10 eligibility. A complete retained result whose key contains
zero may be identified and copied wholesale under BUSY, but zero never
authorizes READY reuse. BUSY compares only `structure_key`; a match publishes
the old complete content, including its zero sequences if any, as STALE_BUSY.
Every complete READY replaces the retained entry. EMPTY, every
`FAILED(reason)` from any phase, every frame/output/resident/allocation budget
failure, and every structural-key change remove retained eligibility before
publication. Only consecutive BUSY publications with no intervening terminal
event may reuse the retained entry. A structural change cannot preserve an old
entry for a later change-back.

#### P-D2 — Stack interval normalization and fragmentation

Using P-D15's `K`, `M`, and `D`, Candidate A defines
`B=floor(M/(D*K*K))`, requires `B>=2`, and has mandatory timestamp capacity
`R_A=K*B`; Candidate B defines `N=floor(M/(D*K))`, requires `N>=2`, and has
mandatory grid-position capacity `R_B=N`. A failed minimum returns
`FAILED(STACK_BUDGET_TOO_SMALL)`. Every multiply/divide is checked before use.

The selected-input count is semantic, not a physical-visit count. For each
member, count every distinct timestamp position contributed after D8 collapse
and nonfinite transformation: retained selected-window source timestamps,
required interpolation brackets, and a synthetic hold endpoint when present.
Candidate A selects at most `B` such positions per member; Candidate B selects
at most `N`. A requested hold endpoint consumes one of those positions during
selection and is never appended as a `B+1`/`N+1` item. The selector must choose
a smaller retained window or return the applicable whole-group failure when
the required brackets/hold cannot fit. Physical samples inspected across every
attempted/selected LOD while finding those positions are counted separately in
`V_observed`, governed by P-D15's prospective `V_limit`/frame allowance, and
never reclassified as selected inputs.

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
| Explicit right hold | For both LINEAR and STEP_AFTER, `HOLD_LAST_FORWARD` extends the latest defined value as a constant through the requested closed right endpoint after duplicate/nonfinite processing. It never creates left extrapolation or crosses a `BREAK_SEGMENT`; `DRAW_NOTHING` adds no extension. |
| Group domain | Normalize every member completely before intersection. A `REJECT_WINDOW` failure is resolved before a would-be empty intersection. Preserve every closed intersection component/shared endpoint, merge only touching components defined at that endpoint, and never bridge a removed point or open gap. An otherwise empty intersection is `EMPTY`. |
| Candidate A fragmentation | Exact union must retain every normalized intersection endpoint and every selected member breakpoint required by the exact union within `R_A=K*B`; none is optional. Excess returns `FAILED(FRAGMENTATION_BUDGET)` before partial output. |
| Candidate B fragmentation | The deterministic grid reserves every mandatory normalized endpoint within `R_B=N`; only remaining positions are optional grid positions. Excess returns `FAILED(FRAGMENTATION_BUDGET)` before partial output. |

#### P-R1 — Result authority, stale presentation, and failure precedence

The D9 `frame_result_t` always contains its ordinary non-stack result section
under the Batch 3C contract. Stack result storage failure never suppresses,
relabels, or changes ordinary planning, rendering, pixels, statuses, counters,
or result publication. The stack extension begins with one fixed-size,
allocation-free `stack_result_section_t` envelope whose closed dispositions are
exactly `COMPLETE`, `FAILED(RESULT_STORAGE_BUDGET)`, and
`FAILED(RESULT_STORAGE_ALLOCATION_FAILED)`.

Before any stack source acquisition, calculate and attempt the exact independent
public stack group/view/member table allocation, counting its complete
instantaneous CPU bytes under P-D15. A configured-cap rejection sets the stack
section to `FAILED(RESULT_STORAGE_BUDGET)`; an in-cap allocation failure sets it
to `FAILED(RESULT_STORAGE_ALLOCATION_FAILED)`. Either stack-section failure has
no stack table, performs no stack acquisition or geometry work, and clears all
stack retained-content eligibility before publication. Ordinary work continues
unchanged. The per-entry P-R1 invariants below apply only when the stack section
is `COMPLETE`.

When the stack section is `COMPLETE`, its `stack_result_table_t` owns every byte
referenced by its immutable group and member views. The table order is all
`Stack_view_kind::MAIN` group/view records by ascending
`lowest_enabled_series_id`, then all `Stack_view_kind::PREVIEW` records in the
same order; every record's members are ascending `series_id`. Each public
`stack_result_entry_t` contains its value-only `stack_group_id` and
`Stack_view_kind`, and each per-series record contains its value-only
`series_id`, including `FAILED(reason)` and `NOT_EVALUATED` records. These IDs are
self-description labels only and never cache, retention, or reuse authority.
The enclosing D9 result supplies the executed plan's frame/config/view identity,
including the producer's `publication_frame_id`; its current-config identity
check rejects a delayed result after configuration or topology changes.

Every `stack_result_entry_t` has exactly one canonical disposition: `READY`,
`EMPTY`, `STALE_BUSY`, or `FAILED(reason)`. Its complete public field set is the
structural labels and disposition; per-series origin, status, sequence, selected
LOD and logical window; rendered range; value-only indicators and their cursor
request IDs; and optional `content_frame_id`. It contains no geometry, spans,
samples, resources, source identities, owner handles, cache keys, or other
private backing.

`stack_result_section_t`, `stack_result_table_t`, `stack_result_entry_t`, and
the per-series record have private storage and no public mutator or unrestricted
constructor. Only the frame producer's named factories/tagged variants may
create them, with these structural invariants:

- `complete(table)` contains exactly one complete table;
  `result_storage_budget()` and `result_storage_allocation_failed()` contain no
  table;
- `ready(presented_payload)` and `stale_busy(retained)` require
  `content_frame_id`, rendered range, and presented indicators with their exact
  request IDs;
- `empty(observations)` and `failed(reason,observations)` contain no
  `content_frame_id`, rendered range, or indicators;
- `not_evaluated(series_id)` contains only its structural label;
- `current_observation(series_id,status,seq,optional_lod_window)` contains
  only source-attempt fields associated with the enclosing
  `publication_frame_id`, and no value, indicator, rendered range, or content
  identity;
- `presented_content(...)` is constructible only inside `READY`/`STALE_BUSY` with
  the enclosing entry's exact `content_frame_id`.

Disposition-matched observers return const/non-owning views; an observer for a
different disposition returns absent/null. Users cannot construct, retag, or
mutate a result into an invalid combination.

Every per-series entry has exactly one origin:

- `NOT_EVALUATED` carries only `series_id` and no observation, content, status,
  sequence, LOD/window, value, indicator, frame ID, or key facts;
- `CURRENT_OBSERVATION` carries only completed current-attempt fields from the
  enclosing result's `publication_frame_id` and carries no value, indicator,
  rendered range, or `content_frame_id`;
- `PRESENTED_CONTENT` carries only complete presented fields associated with
  the entry's exact `content_frame_id`.

READY entries are current `PRESENTED_CONTENT`; STALE_BUSY entries are retained
`PRESENTED_CONTENT`; `EMPTY`/`FAILED(reason)` have no content identity, rendered
range, or indicators and may contain only CURRENT_OBSERVATION for actually
completed attempts. Every pre-acquisition failure has all entries NOT_EVALUATED. A partial
acquisition failure has CURRENT_OBSERVATION only for attempts completed through
the first FAILED and NOT_EVALUATED afterward. Post-composition numeric,
resident-cap, CPU-allocation, and RHI-allocation failures likewise expose no
rendered range or indicators.

For STALE_BUSY, the enclosing result keeps the current `publication_frame_id`
and the public entry's group disposition alone reports that a current BUSY
attempt caused fallback. The renderer reuses private retained geometry and
copies only its value-only retained presentation record—per-series presentation
fields, rendered range, indicators, cursor request IDs, and `content_frame_id`—
into the current independent public stack table. No geometry or private key is
copied into that table, and no current partial acquisition enters presentation;
current attempts exist only in diagnostic trace/counters. The normal GUI
current-request check therefore rejects an old retained cursor request ID
instead of relabelling it as current.

The group never owns an ordered reason set. Evaluation stops once the
controlling phase has its required deterministic observations, performs no
speculative later-phase work, and selects one reason as follows:

1. Validate registry facts: `FAILED(STACK_REQUIRES_TWO_MEMBERS)`; then, for each
   member in ascending enabled series ID, `FAILED(STACK_SOURCE_MISSING)`,
   `FAILED(CUSTOM_LAYER_IN_STACK)`, `FAILED(STACK_REQUIRES_TIMESTAMP)`, and
   `FAILED(STACK_REQUIRES_SCALAR_VALUE)` in that order; then
   `FAILED(STACK_GROUP_HAS_NO_VISIBLE_STYLE)` and
   `FAILED(STACK_BUDGET_TOO_SMALL)`.
2. Apply P-D15 metadata-only frame admission.
   `FAILED(FRAME_BUDGET)` performs no source acquisition and consumes no
   resident resource. Fixed reservations are never refunded based on a later
   disposition.
3. Acquire required members sequentially in ascending enabled series ID. BUSY
   and EMPTY never stop acquisition because a later source FAILED outranks both.
   The first source FAILED maps to `FAILED(SOURCE_FAILED)`, stops acquisition,
   and leaves only later series NOT_EVALUATED; every earlier completed attempt
   is CURRENT_OBSERVATION. If no FAILED occurs, complete every required acquisition. EMPTY
   then outranks BUSY. If neither FAILED nor EMPTY occurred but any source is
   BUSY, discard all fresh READY presentation facts: an identical retained
   `structure_key` yields wholesale STALE_BUSY as defined above; otherwise
   return `FAILED(SOURCE_BUSY)`. The no-retained BUSY result contains all
   required current acquisition entries as CURRENT_OBSERVATION. NOT_EVALUATED may appear
   only after the first source FAILED or a pre-acquisition short-circuit.
4. Validate order and selected windows: `FAILED(UNORDERED_STACK_INPUT)`, then
   `FAILED(LOD_BUDGET_EXCEEDED)`.
5. Normalize every member under D2. `FAILED(NONFINITE_REJECTED)` precedes a
   would-be EMPTY intersection, then `FAILED(FRAGMENTATION_BUDGET)`. Accumulate
   in double; after every addition require a finite double cumulative value,
   convert bottom/top to the v1 float representation, and require each converted
   float to be finite before emitting anything. Exact double-to-float roundtrip
   is not required; normal finite rounding is accepted. Failure is
   `FAILED(STACK_NUMERIC_OVERFLOW)`. GLOBAL/GLOBAL_LOD bounded-query failure is
   `FAILED(STACK_GLOBAL_RANGE_UNBOUNDED)`.
6. Enforce group output: `FAILED(STACK_OUTPUT_BUDGET_EXCEEDED)` if actual output pairs,
   visits, or bytes exceed `M`, `V_limit`, or `H_limit` respectively.
7. Apply P-D15's complete resident transaction. A configured-cap excess is
   `FAILED(RESIDENT_BUDGET)`; an in-cap CPU or QRhi allocation failure is
   `FAILED(RESOURCE_ALLOCATION_FAILED)`. Neither condition leaves partial
   geometry or an installed partial cache entry.

Every P-D15-generated frame/output/resident/allocation failure suppresses
STALE_BUSY and removes the target's retained-content eligibility before result
publication. The same pre-publication removal applies to EMPTY, every other
`FAILED(reason)`, and structural-key change; READY installs the sole replacement
entry. Thus only uninterrupted BUSY after READY/STALE_BUSY can remain stale.

#### P-D15 — Frame admission and resident resource transaction

For framebuffer width `W`, enabled membership `K`, fixed experiment/production
density `C`, and `D=1` for all-LINEAR or `D=2` when STEP_AFTER needs paired
events, define `M=floor(C*W)`, `V_limit=2*M`, and
`A=align_up(sizeof(actual_stack_uniform_std140),rhi->ubufAlignment())` with a
static assertion on the real uniform record. The immutable membership/styles
produce one candidate-independent common draw plan: one group/view common
uniform slot plus exactly one style slot for every scheduled built-in AREA-band,
DOTS-knot, or LINE-boundary draw and no data-dependent optional draw. `U_limit`
is exactly the number of uniform blocks in that plan; both candidates therefore
reserve the same value. `U_observed` is the number actually updated and must
satisfy `U_observed<=U_limit`.

The checked per-group/view reservation is
`H_limit=checked_add(checked_mul(40,M),checked_mul(A,U_limit))`. Actual upload
bytes are
`12*K*E + 8*group_span_count + 8*sum_line(span_length+2) +
sizeof(actual_stack_uniform_std140)*U_observed`; every add/multiply and
`K*E`/visit count is checked. Any arithmetic failure, prospective output/visit/
write excess, or `U_observed>U_limit` returns
`FAILED(STACK_OUTPUT_BUDGET_EXCEEDED)` before the prohibited side effect.

Hard limits are prospective. Admission assigns each accepted `(group,view)` an
immutable reservation slice `{M,V_limit,H_limit}`. Before every physical sample
inspection, checked arithmetic computes `V_observed+1` against that unit's
immutable `V_limit` slice. If the next visit would exceed it,
perform no inspection and return `FAILED(STACK_OUTPUT_BUDGET_EXCEEDED)`;
`V_observed` never exceeds its limit. Apply the same checked-before-side-effect
rule before each output-pair creation (`pairs+1` against its immutable `M`
slice) and each upload write (`bytes+write_size` against its immutable
`H_limit` slice). The rejected limit+1 operation performs no read, write,
allocation growth, counter increment, geometry emission, or other side effect.

One metadata-only frame planner charges the fixed checked reservation
`(M,V_limit,H_limit)` before acquisition/allocation: every MAIN unit in
ascending `lowest_enabled_series_id`, then every PREVIEW unit in the same
order, against `FRAME_M_LIMIT`, `FRAME_V_LIMIT`, and `FRAME_H_LIMIT`. A unit
that cannot fit every total returns `FAILED(FRAME_BUDGET)`, emits no geometry,
and performs no acquisition/allocation. Frame totals are consulted only while
admitting the fixed slices. Runtime work consumes only the admitted unit's own
slice: actual use never changes a shared remaining balance, transfers slack,
refunds capacity, or readmits a rejected unit. Consequently an earlier unit's
low or full actual use cannot change any later admitted unit, and preview
enablement cannot change MAIN admission.

`STACK_CPU_BYTES_LIMIT` counts only stack-exclusive renderer-owned live bytes:
composition arrays, normalized stack-only spans, boundary/padding storage,
uniform/upload staging, the independent public stack-result table while it is
renderer-owned, and private retained stack entries. A private
`renderer_retained_stack_entry_t` owns its P-D6 keys, retained geometry and
resources, and exactly one value-only retained presentation record. Its
presentation record and geometry/resources remain separately counted until
their renderer ownership is released. Renderer builders, temporaries, current
entries, and stale entries remain counted while the renderer owns, references,
or aliases them. Each private allocation is counted exactly once until the
renderer's final reference is released, even if some non-public external handle
also shares it.

Of frame-result publication storage, only the stack group/view/member table is
charged to `STACK_CPU_BYTES_LIMIT`; the ordinary D9 section and its backing are
not.

Before stack work, the renderer allocates the exact independent public stack-
result table required by the `COMPLETE` section. It is populated in-place with
no later renderer-side allocation, including no publication/postprocessing
deep-copy after `READY`/`STALE_BUSY` installation. READY atomically installs the
private geometry/resources and its value-only retained presentation record;
STALE_BUSY reuses that private
geometry and copies only the retained value record into the current public
`stack_result_entry_t`. Atomic frame publication moves the independent public
stack table to GUI/public ownership and debits its bytes simultaneously; the
publication operation allocates nothing and the public table never aliases
private retained backing.

`stack_result_table_t` owns every byte addressed by its read-only, non-owning
group/member slices, and each slice is valid exactly for that table's lifetime.
Publication and move preserve the referenced immutable backing and rebind no
view to renderer-private storage. An external copy either shares the immutable
public backing or performs one complete deep copy and rebases every slice to the
new public backing; no public view ever references
`renderer_retained_stack_entry_t` or any other private renderer allocation. Any
external-consumer copy occurs after renderer ownership has ended. Ordinary D9
result storage and Stage 3 per-series arrays remain excluded from
`STACK_CPU_BYTES_LIMIT`. Capped CPU
storage uses only a narrow repo-local noncopyable
exact-size owning contiguous array—equivalent to
`unique_ptr<T[]>` plus explicit element count—inside stack-exclusive storage.
It is not a general allocator or container framework and ordinary Stage 3
vectors remain unchanged. Requested/committed bytes are therefore exact.
`STACK_QRHI_BYTES_LIMIT` counts the exact requested bytes of renderer-owned
stack vertex, boundary, index, and uniform buffers. Opaque driver objects—
pipelines, shader-resource bindings, and textures—are outside these byte totals
and remain separately observable.

Each cap applies at every instant, including temporary replacement storage.
Before allocation, calculate the complete transaction including retained
unrelated entries, the old target entry, and all requested CPU/QRhi temporaries.
If that peak fits, preserve the old target transactionally until a complete new
set is ready. Otherwise remove and debit only the replaceable old target, then
recheck the full temporary transaction. If it still exceeds a configured cap,
return `FAILED(RESIDENT_BUDGET)` without allocating. If allocation then fails
within the cap, return `FAILED(RESOURCE_ALLOCATION_FAILED)`. Install the new
CPU/QRhi set atomically only after every allocation succeeds. On any
resident-budget or allocation failure, remove/destroy the old target even when
it was initially preserved, and clear that target's stale eligibility. Any
P-D15 frame/output/resident budget failure likewise suppresses stale fallback
and removes target eligibility. No path installs a partial set or claims opaque
driver residency.

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
- every action/gate distinguishes approved D2/D6-D15 text from pending
  P-S1/P-D7/P-D2/P-D6/P-Q1/P-R1/P-D15; no affected RU starts before owner
  ratification and the reviewed ratification record, and D4 remains separately
  evidence-gated;
- governed Markdown/style, `actionlint`, and `git diff --check` pass, and the
  exact documentation commit receives the delegated three-reviewer closure
  required above before Batch 3 coding.

## Execution automation contract

The implementation should make each batch reproducible through one gate runner and machine-readable manifests rather than a collection of undocumented shell commands. The exact script layout may follow repository conventions established in Batch 2, but it must implement these contracts when the corresponding capability first becomes necessary:

1. **Preflight:** record source commit/diff identity, compiler, CMake, Qt, Python, external standards revision/hash, graphics backend/device/driver, dependency commit, relevant environment, and complete invocation. Missing tools such as `actionlint` are explicit environment-bootstrap failures, not missing product specifications. Bootstrap tools with a recorded version/hash rather than relying on an unversioned global installation.
2. **Gate result:** emit a versioned manifest containing batch/checkpoint, start/end time, command, exit status, phase, inputs, artifact hashes, CI run/job URLs, and recovery relationship. Failed or terminated attempts are append-only; a green rerun never overwrites them.
3. **Artifacts:** write raw local evidence below the active build tree's `gate-artifacts/<batch>/<source-identity>/<timestamp>/` directory and upload the same bundle from CI/PR runs. Do not commit raw timing output to the source tree.
4. **Delivery:** local commits and review-branch pushes are allowed before gates close so CI and reviewers can inspect them. Merge to `master` is the protected event and requires the named batch gate and independent approval.
5. **Delegated implementation and review closure:** the primary agent
   orchestrates and does not implement subsequent units. A controlling unit is
   one numbered source action, a predeclared same-change cluster, or a named
   evidence-only unit. No full batch/checkpoint or two named units combine, and
   no atomic cluster splits. Source identity is the exact source hash; evidence
   identity is `(exact source hash,evidence-manifest SHA-256)`. Run three
   independent xhigh reviews of that identity, including one
   architecture/contracts/ownership/missing-primitives audit. Any source or
   manifest change restarts all three. Return concrete amendments to the same
   implementer; evidence-only units make no semantic code changes. All clean
   dispositions name the same exact identity. Fable is ambiguity-only. Record
   unit/action IDs, worker/reviewers, identity, findings/amendments, remediation,
   and dispositions.
6. **Native backend contract (owner correction, 2026-07-10):** retained smoke and timing evidence request Qt's native QRhi backend and accept whichever non-Null implementation the platform selects, including D3D, Metal, Vulkan, OpenGL, or a future API. The actual backend, device, and available driver identity are recorded, but no graphics API is itself a product gate. API-specific runs are optional diagnostics only. Null QRhi remains diagnostic and must never masquerade as retained GPU evidence. Raw timing comparisons use one accepted machine/backend/build fingerprint; a fingerprint mismatch invalidates comparison.
7. **Calibration generation:** generate scenario manifests from the fixed plan
   matrices, dimensions, seeds, warm-up counts, and seven-run protocol. Preserve
   every raw run. Do not remove outliers automatically; an interrupted or
   failed run is retained and a replacement run is a separately identified
   attempt. `RU-2.1-calibration` is evidence-only and its review identity is
   `(exact source hash,proposal-manifest SHA-256)`; any source or manifest change
   restarts all three reviews.
8. **Calibration arithmetic:** deterministic counters whose accepted baseline is zero use exact-zero regression rules. For positive metrics, calculate relative median drift from the two calibration sets as specified below. If a median is zero/nonpositive or below declared measurement resolution, report relative margin as unavailable and propose a resolution-based absolute rule instead. The runner must not silently turn unstable calibration into a permissive threshold; it emits `CALIBRATION_REVIEW_REQUIRED` with the observed drift/resolution and proposed rule for owner approval.
9. **A/B evidence:** mechanically reject a D4 candidate that violates correctness, per-group `M`, `V_limit`, or `H_limit`, or D15 frame-admission bounds. For surviving candidates, generate a numerical comparison, deterministic screenshots, pixel differences, and a side-by-side visual bundle. `RU-4B-evidence` is evidence-only and uses `(exact candidate source hash,evidence-manifest SHA-256)`; any source/manifest change restarts all three reviews. A clean evidence review may recommend but never selects D4.
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

**Closed historical record:** Batch 1B and 1C closed at `vnm_plot@2ec8013`.
Nothing in these subsections is an open action, prerequisite, or merge gate for
current work.

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

The historical migration processed exactly one canonical style rule at a time:

1. retain the no-write checker's enumerated findings and external standards identity;
2. run only that rule's fixer against its enumerated files, never the aggregate pipeline `--write`;
3. require a lexical C/C++ token-equivalence check for every formatting rewrite before build/test; any intended token change requires an explicit plan amendment and leaves this batch for the named Batch 1C checkpoint;
4. inspect and retain the complete diff, then rerun all preceding rules plus the active rule;
5. run `git diff --check`, the initialized Release build, and CTest 21/21;
6. obtain the historical iterative three-xhigh-reviewer loop required for the batch, feeding findings into the next remediation/review round until all three return green; the then-active Fable quota exclusion is historical evidence only and does not govern later review units;
7. continue until the complete canonical pipeline passes without `--write`.

The retained canonical check command was:

  ```powershell
  python C:\plms\varinomics\varinomics-standards\tools\style_pipeline.py `
    --root C:\plms\bsd_licensed\vnm_plot
  ```

The retained standards identities are repository base `f5edc8b` with pipeline SHA-256 `8715313C94D8DCC4257EB3792459BA8D7C759C9CEB6958958B64560FD65CB2F4`, and repaired review commit `ce01c3a` with SHA-256 `38056A555CB3AC8CEE31B633A2F5C68CA6B823885C0EF6B15B95979989498CFC`. Its tests passed 576/576; candidate no-write and `--write` were green with zero changes.

`fix_hanging_indent.py` is quarantined: an isolated replay proved that its operator-return renderer corrupts the `std::string{}` ternary in `tests/test_msdf_lcd_shader_reference.cpp::sample_statement_for_offset()`. Do not run that fixer again until the standards repository has a focused regression test and corrected implementation, or until the affected findings are satisfied by manually reviewed token-equivalent edits. A later green result does not erase the failed aggregate-write execution.

Retained gate result:

- every token-preserving rule checker passes individually; only the explicitly routed Batch 1C naming findings may remain before that checkpoint;
- initialized Release build passes;
- CTest passes 21/21.

Batch 1A–1C are closed. The combined Batch 1B/1C canonical pipeline gate passed at `2ec8013`; current work uses the active canonical style gate independently.

### Batch 1C — Explicit non-behavioral style-gate token repairs

This closed historical checkpoint isolated the checker-enumerated cases that could not be made green safely without changing tokens:

1. Rewrite `tests/test_msdf_lcd_shader_reference.cpp::sample_statement_for_offset()` from its ternary return to an equivalent empty-expression early return followed by the existing non-empty string construction. This avoids the proven `std::string`/ternary parser collision without changing generated shader text; retain the focused LCD shader-reference assertions.
2. If the ordered naming check still reports them, rename only the private `_pad0`, `_pad1`, and `_pad2` members in `src/core/series_renderer.cpp` to checker-approved names without changing declaration order, field types, initialization values, buffer packing, public headers, or exported symbols.
3. Permit only checker-enumerated adjacent C++ string-literal splits that the long-string rule cannot express without introducing an additional literal token. Require a focused diff proving that concatenated bytes, prefixes, and suffixes are unchanged; the initially known scope is one test message in `tests/test_plot_interaction_item.cpp`.
4. For the two initialized constant blocks where the ordered local-declaration and assignment-table formatters require opposite spacing, permit commutative constant-multiplication operand reordering so each derived expression begins with its already declared base unit. Preserve the exact compile-time values and retain the core/snapshot tests; the exact scope is `choose_snap_ns()` in `include/vnm_plot/core/algo.h` and the main/preview origin regression in `tests/test_snapshot_caching.cpp`.
5. In `tests/test_msdf_lcd_shader_reference.cpp::lcd_enabled_statement()`, permit decomposition of the same generated shader bytes into a `std::string` first clause and short `+`-joined literal clauses when required by the hard line-length gate. The focused shader-reference test must prove the complete generated statement remains byte-identical.
6. Permit outer parentheses around the unchanged boolean return expressions in `same_cache_shape()` and `validate_cached_glyph()` when required to satisfy both the long-condition and hanging-indent checkers.
7. In `qrhi_layer_data_key_t::operator==` and `layout_cache_key_t::operator==`, permit a named prefix boolean followed by the remaining short-circuit chain. Preserve clause order so no field is evaluated earlier than in the original expression; retain cache-key equality and renderer cache tests.
8. In `Chrome_renderer::render_preview_overlay()`, permit materializing the existing long-double left product before its existing division and parenthesizing the unchanged right-side product. Preserve arithmetic operator order and retain renderer/preview geometry tests; benchmark numeric calibration must include this boundary in its proposed noise margins.

Do not use this checkpoint for unrelated cleanup.

Retained gate result:

- retain the exact checker findings before each change and prove that no old padding identifier use remains;
- inspect the complete token-changing diff and preserve the existing uniform-layout `static_assert` coverage;
- initialized Release build and CTest pass 21/21;
- complete canonical style pipeline passes without `--write`;
- repaired canonical pipeline with `--write` passes without changing an already green candidate;
- the historical three-xhigh-reviewer loop covered Batch 1B and 1C before their `2ec8013` closure; its temporary Fable exclusion is non-governing for subsequent work.

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

Immediate closure sequencing: run evidence-only `RU-2.1-exact-head/A1` with
identity `(exact source hash,CI/gate-manifest SHA-256)`. A red causal finding
does not mutate A1: create the next numbered `RU-2.1-remediation/A<n>` for that
single causal correction, pass its source-unit gates and exact-hash three-review
loop, then rerun A1 with a new evidence identity. `RU-2.1-calibration` cannot
begin until A1 is clean.

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

`CACHE_KEY_MIGRATION` is unconditionally actions 2–4 plus action 6 and never
splits; A1, A5, A7, and A8 remain separate units.

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

Source-unit gate allocation:

- A1 owns production-object/source searches proving test hooks/vectors/writes
  absent plus hook-enabled ordering parity;
- `CACHE_KEY_MIGRATION` owns key equality/mutation matrices, primary revision
  exactly-once behavior, LINE derivative invalidation/reuse, changed/unchanged
  upload counts, source owning/nonowning/ABA identity, `nonowning_source`, and
  pixel/command parity on one source hash;
- A5 owns stable custom-snapshot unchanged reuse; A7 owns zero/unstable/
  conservative rebuild reasons; A8 owns unconditional custom prepare and
  separately observable custom traffic;
- every source unit also runs the common source-unit gate; no later evidence
  substitutes for its deterministic oracle.

End-to-end gate:

- repeat the approved seven-run protocol with identical inputs;
- deterministic counter win is present;
- every regression metric satisfies the owner-approved numeric noise rule;
- all raw before/after artifacts remain attached to the batch.

After every source unit is clean, `RU-2.2-evidence/A1` runs the fixed before/
after protocol and is identified by
`(exact source hash,before/after-manifest SHA-256)`. It owns numeric comparison
against the owner-approved noise rules, raw artifact hashes, environment/
dependency provenance, and terminal/failure validation under the common
evidence-only gate. Any source or manifest change restarts all three reviews.

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

`SOURCE_API_BREAK` runs first after P-S1 ratification and its clean
ratification record; independent action 6 follows; `D12_HOLD_BREAK` runs last.
The clusters do not overlap.

1. Replace `Data_source_ref` in ordinary and preview descriptors with `shared_ptr<Data_source>`.
2. Remove raw-reference setters/builders without a no-op-deleter bridge.
3. Make `Benchmark_data_source` own/share its `Ring_buffer`; changing only the source pointer type would leave a teardown UAF.
4. Remove ambiguous `Data_source::snapshot()` and `Data_source::identity()`; migrate their production/cache callers atomically to explicit status handling and the D14 owner/object/sequence/semantics facts.
5. After P-S1 ratification, implement its top-level snapshot-result sequence
   and named factories exactly; no local action text defines a second factory
   contract.
6. Publish and enforce the D11 GUI-thread-only widget contract, preserve the
   separate internally synchronized `Data_source` concurrency contract, and add
   queued-cross-thread/illegal-direct-call oracles in the same source unit; this
   is not delegated as documentation alone.
7. Publish D12's one-object acquisition-domain rule with the exact acquiring and non-acquiring method lists from the register, the same-object no-recursion rule, and the shared-backing conformance rule. No capability enum or domain token is added.
8. Replace the owning custom-layer prepare window with a call-scoped structural view passed by reference: metadata and non-owning spans only, with no snapshot, hold, owner, guard, or other ownership-capable field. Remove raw snapshot access from the record context; layer-prepared/GPU state and copied value metadata remain available for recording.
9. In the same D12_HOLD_BREAK cluster, remove `cached_snapshot_hold` and every snapshot/hold-owning field from the current per-series `sample_window_t`, `Series_view_plan`, renderer cache, and record context. The existing per-series planner may acquire independently, but each observation is fully consumed into owned derived CPU/custom-prepared state and released before another same-source acquisition and before record; no surviving plan/cache/context holds producer storage. This does not add a frame/shared-key scheduler.
10. Migrate ordinary/preview examples, benchmark ownership, package consumers,
    and ordinary source/status tests in `SOURCE_API_BREAK`; no compatibility
    shim or dormant alternate source API remains.
11. Migrate custom-layer consumers, lifecycle tests, and record-context
    assertions in `D12_HOLD_BREAK`; no hold-capable alternate custom context
    remains.

Gate:

- `rg "Data_source_ref|set_data_source_ref|data_source_ref\("` has no public, production, example, or benchmark hits, and source search proves the `Data_source::identity()` virtual and every cache call/field are absent;
- Ubuntu Clang ASan (`-fsanitize=address`) ordinary/preview lifetime and benchmark teardown pass;
- Ubuntu Clang TSan core/update-remove stress is run with `-fsanitize=thread`; if Qt/QRhi cannot link or run under TSan, record `UNSUPPORTED` with compiler/linker evidence and run the same stress test without sanitizer on all four CI platforms;
- after P-S1 ratification, four-status construction tests prove its exact
  factories: READY cannot coexist with an invalid snapshot and copies payload
  sequence; EMPTY nonzero is an exact empty revision while zero is explicitly
  unavailable/unstable; BUSY/FAILED carry no payload and zero sequence; no
  caller reads `current_sequence()` post hoc; compile/API tests prove private
  construction/storage, no public mutation, read-only observers, READY-only
  contextual truth, and nonowning pointer/null payload access;
- an actual shared-lock-backed source proves no recursive same-object call; distinct objects sharing backing either return independently acquired holds or fail the conformance test; metadata/current-sequence/LOD/time-order calls remain safe while a hold exists; rejected attempts release before the next acquisition;
- custom-layer tests prove prepare data is valid only for the call, prepare cannot retain a hold through its public type, and record has no raw snapshot access;
- source search proves current per-series plans/caches/record contexts own no snapshot/hold; a lock-backed source proves the writer proceeds after per-series derivation/custom prepare and before record, and teardown/next same-source acquisition sees no surviving hold. Cross-series one-acquisition sharing is not claimed until RU-3C2;
- Qt tests and public documentation enforce GUI-thread-only widget access while source content remains concurrently readable;
- repeat the approved Stage 2 full-path protocol; source wait and latency satisfy its numeric rule.

Gate allocation:

- `SOURCE_API_BREAK` owns the no-raw/no-identity searches, P-S1 factory/status
  oracles, ordinary/preview lifetime ASan, benchmark teardown, migrated ordinary
  consumers/tests, and public install/package consumer smoke;
- `RU-3A/A6` owns D11 GUI enforcement, queued invocation, illegal direct-call,
  and independent source-content concurrency tests;
- `D12_HOLD_BREAK` owns the lock-backed acquisition-domain/no-recursion/writer-
  progress gates, custom structural-borrow/record tests, no-surviving-hold
  search, lifecycle migration, and TSan or recorded-UNSUPPORTED fallback;
- each unit also runs the common source-unit gate; no declarative action is
  delegated or accepted without its enforcement oracle.

### Batch 3B — One evaluator and one range producer

Primary files:

- `include/vnm_plot/core/algo.h` and access/query types;
- `src/core/types.cpp`;
- `src/core/auto_range_resolver.cpp`;
- `src/qt/plot_widget.cpp`;
- focused range/interaction tests.

Prerequisite: approved D7/D8 govern conversion and duplicate collapse, but
P-Q1 and P-D7 both require explicit owner ratification and a clean ratification
record before RU-3B begins. D9 renderer-pinned publication belongs to RU-3C1,
where the shared frame plan exists.

Hypothesis: one canonical evaluator preserves render semantics while indexed sources eliminate full LOD-0 UI copies/scans; the measurement boundary is evaluator/range request through its canonical value result.

Actions:

1. Add the sole `resolve_sample_at` point producer implementing P-Q1 exactly.
2. Preserve GLOBAL value-only/range-only access without timestamps in canonical `Data_source::query_v_range()`.
3. Enforce and test that time-bounded VISIBLE and point requests require
   timestamp access while value-only all-time range remains timestamp-free.
4. Route auto-adjust/range consumers through canonical `query_v_range`; do not
   migrate point consumers or add renderer-pinned D9 publication in this
   action.
5. Delete the private range scanner only after value-only/all-time parity is proven.
6. Add public `checked_seconds_to_ns(double) -> optional<int64_t>` exactly as D7
   defines and implement P-D7's exact member-admissibility trait; migrate
   floating-member callers to checked ingestion into integral-nanosecond
   storage.
7. Add P-Q1's exact public request/context/value types,
   `Sample_query_failure_reason`, invariant-safe `sample_query_result_t` named
   factories, and virtual `query_sample` status/sequence contract; do not use
   generic mutable `data_query_result<T>` for point queries.
8. Implement P-Q1's exact single-acquisition default, ordered/UNKNOWN/UNORDERED
   behavior, bounded point support, nonfinite search, hold, and direct-override
   rules.
9. Apply D8 duplicate collapse before P-Q1 nonfinite/interpolation/hold
   processing in both traversal directions and across split snapshots.
10. Replace duplicate local time constants and `floor_div` with the canonical
    time-unit implementation; this action changes no query consumer or docs.
11. Migrate non-pixel-parity point/cursor consumers to the canonical point
    producer; do not add D9 renderer-pinned publication or another snapshot
    path.
12. Document only the P-Q1 public point API, status/sequence, support/search,
    ordering, and migration contract.
13. Document D7/P-D7 ingestion/member admissibility and the separate
    `query_v_range` range/auto-adjust contract.

Gate:

- existing GLOBAL value-only test remains green;
- unordered `[0,90,10,100]`, query 11 resolves nearest timestamp 10;
- ascending, descending, wrapped-segment duplicates (greatest logical `at(i)` wins), nonfinite-winner behavior, hold, expected-sequence mismatch, direct/default status parity, and all statuses pass;
- factory tests make invalid point status/value/reason/sequence combinations
  unrepresentable and prove READY/EMPTY exact sequence, EMPTY zero semantics,
  BUSY/UNSUPPORTED zero/no-reason, and FAILED reason mapping for source failure,
  malformed source result, and point REJECT; compile/API tests prove the failure
  enum has exactly those three reasons, private storage/construction, no public
  mutation, `Data_query_status`, read-only observers, contextual READY truth,
  and disposition-matched nonowning pointer/null access;
- P-Q1 point tests distinguish physical visits from semantic support: a near
  finite chosen winner plus far nonfinite visited history succeeds; a chosen
  nonfinite winner rejects, zeros, skips, or BREAK-removes exactly by policy;
  counted outward
  SKIP and BREAK-aware interpolation, globally closest NEAREST outside the
  interpolation domain, gap/no-left INTERPOLATE, constant LINEAR/STEP_AFTER
  right hold, snapshot-local UNKNOWN, default UNORDERED NEAREST scan and
  INTERPOLATE UNSUPPORTED, and direct-override parity all pass;
- equidistant NEAREST cases in ascending, descending, unordered, negative-time, wrapped-duplicate, and signed-extreme inputs choose the greater timestamp using overflow-safe unsigned distance; one-nanosecond-off-midpoint cases choose the strictly nearer timestamp;
- expected-sequence mismatch performs exactly one `query_sample` call for that
  invocation, publishes nothing, and performs no synchronous retry/spin; a later
  independently scheduled request is a separately counted invocation;
- D7 uses an exact-rational oracle derived from the input binary64 bits. Exact halves such as `seconds=+/-1/1024` round away from zero; `std::nextafter` neighbours around both signed rounded-result boundaries establish representable accepted/rejected inputs. P-D7 compile-time detection applies `remove_reference_t` then `remove_cv_t`, rejects volatile and bool, accepts const and fitting integral signed/unsigned ranges, and rejects floating/`uint64_t`/wider types; NaN/infinities return `nullopt`, and UBSan observes no invalid/hot-loop conversion;
- duplicate scanner is absent only after parity;
- a direct indexed million-sample `query_sample` cursor gate is bounded and returns the same values/statuses/sequences as the default evaluator; separately, the `query_v_range` million-sample auto-adjust gate proves its own bounded direct override and reports fallback cost. Neither gate substitutes for the other.

Gate allocation:

- `POINT_QUERY_CONTRACT` owns all point result-factory, status/reason/sequence,
  semantic-support versus physical-visit, duplicate/nonfinite, ordering/tie,
  interpolation/hold, expected-sequence, default/direct parity, point-consumer,
  point-doc, and million-sample `query_sample` cursor gates;
- `RU-3B/A2` owns GLOBAL value-only/all-time `query_v_range` behavior;
  `RU-3B/A3` owns executable rejection/acceptance tests for timestamp-required
  versus value-only requests; `RU-3B/A4` owns range-consumer parity and the
  separate million-sample range/auto-adjust gate; `RU-3B/A5` owns private-scanner
  absence after retained parity;
- `D7_INGESTION` owns the exact-rational/`nextafter`/UBSan conversion oracle,
  const/volatile/reference/integral trait compile detection, migrated ingestion
  callers, normative docs, and no-hot-loop-conversion proof;
- `RU-3B/A10` owns canonical time-unit/floor-division focused parity and warning
  cleanup. Every unit also runs the common source-unit gate; no prose-only
  action is delegated independently.

### Batch 3C — Shared frame plan and range/render identity

Primary files:

- `src/core/series_window_planner.*`;
- new internal `frame_series_planner.*` only if it immediately replaces current production planning;
- frame range and renderer paths;
- benchmark production-planner entry;
- focused cache/layout tests.

Hypothesis: one production frame entry and snapshot-free immutable plan remove duplicate snapshots/scans and ordinary range/render/result disagreement without increasing acquisition cost as retained history grows. The full-path boundary is production planning entry through executed renderer/RHI disposition and the ordinary D9 immutable frame result. RU-3C1 has no P-R1/P-D15 or stack-result-storage prerequisite.

Actions:

1. Use full pre-layout framebuffer width as the deterministic LOD-budget width; final usable width changes transforms, not selected LOD.
2. **3C1 parity:** add one production frame entry that creates one immutable per-series/per-view plan consumed by VISIBLE range and rendering. The plan owns status, sequence, selected LOD/window/spans, extrema, access/semantic facts, and derived normalized data, but no snapshot or hold.
3. **3C1 result:** after renderer/RHI disposition is known, construct the sole immutable ordinary D9 frame result from the exact executed plan rather than accepting independent frame/config/view identities. Publish D9 value-only indicators with cursor request ID; the GUI discards a non-current request. No independent ordinary range, indicator, or status producer remains after parity. Do not add P-R1's stack envelope or make ordinary publication depend on a stack proposal in this unit.
4. **3C1 evidence entry:** route the benchmark through the same production frame entry before generating parity evidence. Record the visible full-framebuffer-width LOD change explicitly rather than treating it as an invisible refactor.
5. **3C2 acquisition/reuse:** implement the sole production D12 scheduler. Cache attempted status, sequence, and counters without retaining rejected holds; acquire each shared `(source,LOD)` observation once across series, then consume distinct same-source LODs/main-preview queries sequentially. Finish extrema/staging/custom prepare and destroy every hold before another acquisition on that source and before render-pass recording. Do not cache direct query results without their complete request/context/expected-sequence key.
6. Replace RU-3A's truthful hold-free per-series independent acquisitions with the single frame/shared-key schedule from action 5, consuming the existing snapshot-free plan/cache/custom interfaces unchanged. Do not introduce a parallel scheduler or restore a hold-bearing plan.
7. After P-D6 ratification and its clean ratification record, implement its
   compact `structure_key`/`content_key` exactly for BUSY fallback and
   READY/geometry reuse with D10 eligibility. Cache UNKNOWN classification only
   under P-D6's weak-owner/alias source identity, stable nonzero observed
   sequence, and access semantics.
   D13 governs observed absence; do not add source identity/incarnation/revision
   tokens.
8. Update README VISIBLE/frame-plan/result/acquisition behavior with the production change.

Gate:

- the production benchmark, ordinary VISIBLE range, renderer geometry, D9 indicators, and ordinary frame result use the same executed plan, LOD, sequence, window, and actual disposition, independently of P-R1/P-D15 stack storage;
- ten series sharing one source/LOD call `try_snapshot()` once per frame;
- attempted-LOD counters prove rejected holds release before the next acquisition; same-source main/preview with distinct LODs are consumed sequentially without overlap and may truthfully report different sequences;
- alternating one-segment/two-segment snapshots with identical logical
  `at(i)` content, logical `(first,count)`, stable sequence, hold, origin, and
  versions produce the same P-D6 `content_key` and READY reuse; changing a
  logical key field invalidates;
- P-D6 identity tests prove weak-owner expiry invalidates use without lifetime
  extension, the same control block with different alias pointers does not
  match, different control blocks at a reused identical raw address do not
  match, owner-equivalent weak handles plus equal alias do match, and no key
  serializes/hashes a control-block address;
- shared custom prepare finishes while its non-owning view is valid, the writer proceeds before record, no plan/result/cache/record context owns a hold, and source search proves exactly one production acquisition scheduler;
- delayed cursor results are discarded by request ID, and frame-result identity fields are copied only from the executed plan;
- at fixed W/visible interval, run 1x/10x/100x retained histories for direct/zero-copy, copy-on-snapshot, and unknown-order sources after a changed sequence: exact snapshot bytes/physical visits at 100x must not exceed the 1x value, and time/producer-lock metrics must satisfy the approved Stage 2 noise rule for any source class eligible for stacking; fallback classes that fail remain measured but are not silently declared stack-safe;
- warning-clean initialized build and all focused tests pass;
- the approved Stage 2 protocol passes for full-frame latency, allocations, snapshot bytes/time, alignment scans, producer wait, and memory.

Gate allocation:

- `RU-3C1/A1` owns full-framebuffer-width selection and regular/irregular LOD
  before/after pixel evidence;
- `FRAME_TRUTH_PARITY` (actions 2–4) owns one production entry, snapshot-free
  ordinary plan, ordinary VISIBLE/render/result/indicator identity, benchmark production entry,
  delayed-request rejection, and removal of independent range/indicator/status
  producers; it neither owns nor waits for the stack result envelope;
- `SHARED_ACQUISITION_SCHEDULER` (actions 5–8) owns exactly-one-scheduler and
  shared-key acquisition counts, D12 sequential release/writer progress,
  P-D6 logical-key versus physical-segmentation reuse, classification/cache
  eligibility, bounded-history cost, and normative scheduler documentation;
- each unit also runs the common source-unit gate; parity evidence cannot excuse
  an unreviewed scheduler change and scheduler evidence cannot retroactively
  justify frame-truth code.

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
3. Retain and test individual add/remove as committed intermediate topology and
   the batch API as the atomic caller path for stack-membership changes.
4. Copy map/config only when its publication revision changes.
5. Geometry equality continues to use registration slot, owning source owner/object, sequence, access semantics, and plan/window fields; global map revision, color, and label do not invalidate geometry.
6. Conservative callable access without a stable semantics key still invalidates safely.
7. Once D11 enforcement and migrated tests are green, remove widget mutexes made redundant by GUI-thread ownership; do not add a second publication model.
8. Update the README example to use the canonical widget setter for widget-owned
   fields and queued cross-thread invocation, and compile/run that example in a
   focused documentation smoke; this is not delegated as prose alone.

Gate:

- one initial map/config copy, then zero until mutation;
- one mixed add/update/remove call publishes one revision and contains no null map entries;
- GUI-thread enforcement and queued cross-thread invocation tests pass before redundant widget mutexes are removed; source-content concurrency remains independently tested;
- color/label-only stable-semantic update causes no snapshot/range scan/geometry upload;
- conservative callable clone invalidates;
- pixel/command output is unchanged;
- approved Stage 2 synchronization/full-frame metrics satisfy the numeric rule.

Gate allocation:

- A1 owns initial/changed series-map revision counts; A2 owns mixed atomic
  add/update/remove publication and null-erasure; A3 owns individual-intermediate
  versus caller-batched topology tests; A4 owns initial-one/unchanged-zero copy
  counts;
- A5 owns geometry-key and color/label no-snapshot/no-upload parity; A6 owns
  conservative semantic invalidation; A7 owns D11 enforcement-before-mutex-
  removal, queued cross-thread, source-concurrency, and synchronization stress;
  A8 owns the compiled/runnable README setter/queued-invocation smoke;
- every numbered action is an independent source unit with its allocated oracle
  plus the common source-unit gate; no declarative action is delegated alone.

## Stage 4 — Settle the stack design with evidence

Stage 4 is private/core evidence work, not public stack exposure. It may
implement both bounded candidates behind benchmark/internal entry points solely
to execute the register's semantics and select D4. It adds no public stack
metadata/API, and the losing candidate is deleted before Stage 5.

### Contract-ratification record — implementation prerequisite

After the owner explicitly ratifies or rejects P-D2, P-R1, and P-D15,
`RU-contract-ratification-record/A1` records the exact accepted/rejected text
and statuses in the sole register, updates every affected prerequisite, and
numbers the now-authorized RU-4A source actions. This documentation source unit
receives the normal three-reviewer exact-hash loop. No RU-4A or stack candidate
implementation may infer actions directly from the current scope bullets.

### Batch 4A — Core stack contract implementation

Batch 4A cannot begin until P-D2, P-R1, and P-D15 are owner-ratified and the
reviewed ratification record has numbered its executable actions. The bullets
below are a scope inventory only, not review units or implementation authority.
Authorized actions implement the approved backend-independent semantics and
typed results without public RHI/Qt exposure; D4 remains separately open for
evidence. D3 uses `std::optional<int> stack_group_id` with ascending existing
series ID and caller-batched topology; do not add a group descriptor. Scope:

- component order;
- scalar/range projection;
- D8 logical duplicate collapse before nonfinite processing;
- nonfinite and hold behavior;
- the register's complete D2 interval normalization, endpoint inclusion/merge, no-gap-bridging, Candidate A `R_A=K*B` and Candidate B `R_B=N` mandatory-endpoint limits, and `FAILED(FRAGMENTATION_BUDGET)` whole-group result;
- cumulative nonfinite/overflow as a typed whole-group failure with no partial geometry;
- D6 BUSY wholesale reuse, private retained-key eligibility, self-describing
  public group/view/member labels and ordering, retained content-frame/per-
  series-sequence provenance, and `STALE_BUSY` disposition;
- singleton, all-NONE, and custom-layer failures;
- GLOBAL/GLOBAL_LOD common-domain behavior;
- cross-source/cross-LOD non-atomic snapshot wording;
- preview and indicator behavior;
- exactly one P-R1/D15 stack disposition per `(group,view)` under a `COMPLETE`
  stack section and the register's deterministic validation/admission/
  acquisition/domain/output precedence; no failure-reason set or speculative
  later-stage evaluation. Ordinary D9 results remain independent.

The focused tests are product oracles for the approved contracts. Include
reason-peeling/multi-failure cases, FAILED-over-EMPTY-over-BUSY source cases,
fragmentation plus would-be overflow, and proof that a frame-rejected unit makes
no acquisition. This batch does not select D4, add a public stack API, or add an
alternate result/status channel.

After pending-contract ratification, executable D2/P-R1 cases include LINEAR
and STEP_AFTER constant right hold versus DRAW_NOTHING; no left/cross-break
extension; REJECT failure before would-be EMPTY; normalization-before-intersect;
Candidate A retaining every intersection endpoint and selected breakpoint with
exact `R_A` overflow; Candidate B mandatory endpoints before optional positions
with exact `R_B` overflow; FAILED/EMPTY/BUSY precedence; NOT_EVALUATED,
CURRENT_OBSERVATION, and PRESENTED_CONTENT origins; and double/float-max/negative/cancellation/ordinary-rounding
representability with no partial emit.

The ratified P-D2 gates also exercise exact `B`/`N` selected-input counts at the
limit, below it, and one over; required interpolation brackets; and requested
hold endpoints that consume a position, force a smaller retained window, or
fail whole-group—never a hidden `+1`. Separate `V_observed` assertions prove
physical visits do not change selected-input counts.

The ratified P-R1 gates permute READY/BUSY/EMPTY/FAILED by ascending series ID:
BUSY/EMPTY continue, the first FAILED stops and marks only later entries
NOT_EVALUATED, EMPTY outranks BUSY after all required attempts, and no-retained
BUSY publishes all attempts CURRENT_OBSERVATION as `FAILED(SOURCE_BUSY)`.
Generation tests prove every enclosing result has current publication identity,
READY has current content identity, EMPTY/FAILED have none, and STALE has the
current outer publication plus wholly retained value-only public entries/range/
indicators while its geometry remains private. Table tests prove all MAIN
records precede ordered PREVIEW records, each class orders groups by
`lowest_enabled_series_id`, and members order by `series_id`. Every group/view
record carries `stack_group_id` and `Stack_view_kind`; every member, including
`FAILED(reason)`/`NOT_EVALUATED` paths, carries `series_id`. Delayed result and
topology-change tests prove those labels remain self-describing while the
enclosing executed-plan config identity rejects stale consumption; labels never
authorize reuse.

Origin tests prove NOT_EVALUATED has only `series_id`, CURRENT_OBSERVATION has
only `series_id` plus source-attempt status/sequence/optional LOD/window
associated with the enclosing `publication_frame_id` and no value, indicator,
range, or content identity, and PRESENTED_CONTENT exists only inside
READY/STALE_BUSY with the exact `content_frame_id`. A retained old cursor
request ID remains old and is rejected by the GUI current-request check.
Compile/API gates prove `stack_result_entry_t` contains only structural IDs,
view kind, disposition, per-series origin/status/sequence/LOD/window, rendered
range, value-only indicators/request IDs, and `content_frame_id`; it contains
and accepts no geometry/span/sample/resource type, source identity, weak owner
handle, alias pointer, `structure_key`, or `content_key`. The private
`renderer_retained_stack_entry_t` alone owns P-D6 keys, geometry/resources, and
one retained value-only presentation record.

Exhaustive construction/API gates prove section/table/entry/member storage is
private, public mutators/unrestricted constructors do not exist, and only the
frame producer's named factories/tagged variants create the allowed states.
Disposition-matched const observers expose the required payload; every
mismatched observer is absent/null. `complete(table)` has exactly one table;
both result-storage failures have none. READY/STALE_BUSY require complete
presented payload, while EMPTY, every `FAILED(reason)`, and post-composition
numeric/resident/CPU/RHI allocation failure expose no content ID, rendered
range, or indicator.

Lifetime gates prove `stack_result_table_t` owns every byte reached by group/
member slices, slices remain valid through publication and move for exactly the
owning table lifetime, and external copy either shares immutable public backing
or deep-copies and rebases every slice. Address/ownership probes prove no public
view aliases private retained renderer state.
Transition gates prove READY→BUSY→BUSY may publish stale, while READY→EMPTY→BUSY,
READY→source FAILED→BUSY, READY→normalization FAILED→BUSY,
READY→budget/allocation FAILED→BUSY, and READY(key A)→structural key B→BUSY(key
A) cannot.
Each terminal/structural event removes eligibility before its own publication.
First-frame and replacement result-storage fault gates prove exact-table cap
rejection versus in-cap allocation failure publish the fixed allocation-free
`stack_result_section_t` as `FAILED(RESULT_STORAGE_BUDGET)` or
`FAILED(RESULT_STORAGE_ALLOCATION_FAILED)` with no stack table/acquisition/
geometry; replacement faults clear every stack stale entry. Mixed ordinary/
stack tests prove ordinary pixels, statuses, counters, planning, rendering, and
results remain identical under either stack-section fault. Successful
construction counts exact instantaneous stack-table bytes before `COMPLETE`
P-R1 entries become available and performs no post-install table allocation.

### Batch 4B — Sampling A/B experiment

In this batch, extend the internal planner request with separate `render_width_px` and optional `max_visible_samples`. Ordinary series pass no cap and retain the existing closest-to-one-pixel `choose_lod_level()` behavior; focused irregular-ladder and coarsest-over-budget tests require identical ordinary LOD/window/pixels. The private stack paths pass B/N without overloading shader width.

Build two private compositor functions in the existing benchmark using that canonical selector/evaluator, plus one benchmark-private QRhi upload/draw adapter and shader shared by both candidates with the simple interleaved layout intended for Stage 5. This makes native GPU-finished and visual A/B evidence real without adding a public interface, strategy enum, planner hierarchy, or target. Candidate cleanup occurs only in the separately reviewed RU-4B-cleanup after owner D4; retain both candidate paths through evidence review and owner selection.

Numbered review units:

1. `RU-4B-common/A1`: implement the common production-selector wrapper,
   benchmark-private QRhi adapter/shader, fixed draw plan, instrumentation,
   scenario schema, and candidate-neutral mechanical oracles. Review this source
   hash independently before either candidate evidence is accepted.
2. `RU-4B-A/A1`: implement Candidate A against the already clean common path,
   with its exact capacity and correctness tests; review independently.
3. `RU-4B-B/A1`: implement Candidate B against the same common path, freeze its
   exact grid construction, and add its capacity/correctness tests; review
   independently.
4. `RU-4B-evidence/A1`: after actions 1–3 are independently clean, run the
   fixed comparison and create the evidence manifest. This evidence-only unit
   changes no semantic code and is identified by
   `(exact candidate source hash,evidence-manifest SHA-256)`. Any source or
   manifest change restarts all three evidence reviews. A clean evidence unit
   establishes trustworthy comparison evidence; it does not select D4.

The three source units implement the then-ratified P-D2/P-R1/P-D15 register
text exactly; the evidence unit verifies that implementation without changing
it. The register is the sole formula, admission, result, and resident resource
authority; this batch creates no duplicate definitions. The fixed scenario
manifest supplies `C`, frame totals, workloads, and all other evidence inputs
identically for both candidates.

Both prototypes call the same benchmark-local wrapper around the production LOD/window selector with explicit `max_visible_samples`; only composition differs. P-D2 semantic selected source timestamps, required brackets, and a synthetic hold endpoint consume B/N. Separately, `V_observed` counts every physical inspection across all attempted and selected LODs under the prospective visit limit; no physical inspection is ever a selected-input position. D12 applies: attempted status/sequence/counters are retained, rejected holds release immediately, and selected observations are consumed sequentially with no hold in the final plan or prototype result.

Candidate A — budgeted event union:

- use P-D2's exact `B` and `R_A` capacity;
- shared selector chooses each source-local LOD independently and enforces
  P-D2's semantic selected-position capacity; every physical inspection is
  counted only in `V_observed` against `V_limit`;
- event union exact relative to selected curves;
- STEP pairs are emitted consecutively in left/right vector order at the same timestamp;
- `E<=D*K*B`, `K*E<=M`.

Candidate B — bounded shared grid:

- use P-D2's exact `N` and `R_B` capacity;
- shared selector chooses each source-local LOD independently and enforces
  P-D2's semantic selected-position capacity; every physical inspection is
  counted only in `V_observed` against `V_limit`;
- `E<=D*N`, `K*E<=M`;
- STEP changes use consecutive left/right vector-order events at the sampled grid transition;
- grid approximation and step displacement are explicit.

Before Candidate B implementation/evidence, freeze one deterministic grid
construction covering the effective interval-set domain, origin, endpoint
inclusion, widened timestamp arithmetic, allocation across disjoint spans, and
the `FAILED(FRAGMENTATION_BUDGET)` result when mandatory endpoints alone exceed N. The
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
- deterministic visual artifacts and hashes for spikes, steps, gaps, and
  cancellation; the evidence unit stores no human verdict.

Decision gate:

- uncapped ordinary series retain identical selected LODs, windows, and pixels on regular/irregular ladders and coarsest-over-budget cases;
- both candidates respect `M`, `V_limit`, `U_limit`, and `H_limit` or return `FAILED(STACK_OUTPUT_BUDGET_EXCEEDED)`;
- exact-boundary scenarios prove Candidate A never selects more than `B` and
  Candidate B never more than `N` semantic input positions per member; brackets
  and hold endpoints consume those capacities, while additional physical search
  visits appear only in `V_observed`;
- prospective-limit tests allow exactly `V_limit` inspections, `M` output pairs,
  and `H_limit` upload bytes, then reject the next operation before its read/
  create/write/counter side effect; `V_observed` and other counters never exceed
  their immutable per-unit slices;
- multi-group scenarios use identical proposed P-D15 order and `FRAME_M_LIMIT`/`FRAME_V_LIMIT`/`FRAME_H_LIMIT`; rejected `(group,view)` units report `FAILED(FRAME_BUDGET)`, perform no acquisition/allocation, and leave no partial geometry;
- a two-unit admitted replay gives the first unit low actual use and then its
  full slice; the second unit's admission, slice, work, disposition, counters,
  and output remain identical, proving no shared runtime balance, slack
  transfer, refund, or readmission;
- common-adapter tests prove P-D15's stack-exclusive CPU scope, exact requested
  QRhi scope, instantaneous temporary-inclusive caps, preserve-old and
  debit-old replacement paths, narrow exact-array ownership/counting, exclusion
  of ordinary Stage 3 arrays, result construction/publication ownership
  boundary, exact independent public stack-table/no-alias behavior, private
  retained geometry plus value-record ownership, no post-install renderer-side
  deep-copy allocation, table-owned read-only slice lifetime through
  publication/move,
  immutable-public-share or deep-copy/rebase behavior, internal shared-backing
  counted-once behavior, final renderer-release debit, no
  transient uncounted escape, absent
  entry/stale suppression after every budget/allocation failure, configured-cap
  `FAILED(RESIDENT_BUDGET)`, in-cap CPU/QRhi
  `FAILED(RESOURCE_ALLOCATION_FAILED)`, and atomic complete install;
- enabling/disabling previews leaves the admitted MAIN set and all MAIN counters unchanged; a constrained replay proves all MAIN units run in ascending `lowest_enabled_series_id` before any equivalently ordered PREVIEW unit;
- both repeat the approved Stage 2 seven-run native-backend protocol with identical workloads and candidate-specific B/N caps;
- present both candidates' performance and visual evidence without choosing a
  winner in the runner or reviewer disposition;
- do not expose C or a strategy switch unless evidence proves applications need it.

Owner D4 is a separate non-delegated decision bound to the exact
`RU-4B-evidence/A1` identity. The owner reviews those exact artifacts, records
the comparative visual verdict, and selects the winner in the same decision;
workers/reviewers cannot manufacture that verdict. Only afterward,
`RU-4B-cleanup/A1` records why the loser was rejected, deletes its function,
flags, mechanical checks, and candidate-only documentation, binds the winner to
the decision, and proves the losing path absent before Stage 5.

## Stage 5 — Land one complete stack feature

### Batch 5 — Atomic end-to-end feature delivery

Delivery is sequenced through `RU-5-production`, `RU-5-public`, and
`RU-5-final-evidence`; they are distinct review containers and must not be
combined into one review unit. Complete the internal production replacement
before public exposure, and do not merge or deliver public metadata, an unused
planner, or an unused shader separately. Within each container, the numbered
actions recorded before delegation remain the controlling review units unless
this plan predeclares an atomic cluster under the review-unit rule.

Hypothesis and full-path boundary: the selected bounded strategy renders the owner-approved cumulative function within per-group/view `M`, `V_limit`, and `H_limit`, preserves non-stack behavior, and keeps main/preview producer throughput within the Stage 2 numeric rule. Measure from source publication through frame planning, composition, uploads, GPU finish, preview, and frame-published indicator results.

The current numbered source/evidence units are complete generic boundaries and
require no deferred winner-specific planning unit after D4. They may not be
merged, split, or left unnumbered.

`RU-5-production` source actions:

1. **A1 — promotion:** move/promote the winning compositor and shared QRhi
   adapter/shader into canonical layout/RHI targets, switch the benchmark to the
   production producer, and delete all benchmark-local winner copies in the
   same source unit.
2. **A2 — internal frame integration:** integrate the selected bounded planner, exact
   ratified P-D6/P-D2/P-R1/P-D15 contracts, VISIBLE plan identity, bounded
   GLOBAL/GLOBAL_LOD scalar range, cache identities, and stack observations into
   an internal frame producer immediately consumed by the benchmark/internal
   entry. It has no public metadata or unreachable dormant path.
3. **A3 — internal RHI integration:** install the one 12-byte interleaved group VBO,
   AREA/DOTS/LINE draw paths, boundary padding/bindings, and P-D15's exact
   resident transaction in the internal RHI path immediately consumed by that
   benchmark/internal entry.

`RU-5-public` source actions, only after all production actions are clean:

1. **A1 — activation metadata/reachability:** add `std::optional<int>
   stack_group_id` to ordinary descriptors/builders with ascending ID order and
   existing `apply_series_updates` topology semantics, and connect registry/
   frame entry to the already consumed internal production path.
2. **A2 — activation Qt/result/contract:** add the immutable ratified P-R1
   stack-result extension to the already complete ordinary D9 frame result,
   expose cumulative indicators, Qt registry/update behavior, and
   independent main/preview planning; a missing/failed preview member suppresses
   only the complete preview group. In the same cluster publish all normative
   public API/migration/package documentation and clean install/package consumer
   smoke. Actions 1–2 are the atomic `STACK_PUBLIC_ACTIVATION`; neither may land
   unused, unreachable, undocumented, or separately reviewed.
3. **A3 — non-contractual examples/evidence:** after activation is clean, extend
   `examples/preview_config` and tutorial material and add only supplementary
   package evidence. It may not define required API behavior, normative public
   documentation, package surface, or reachability omitted from activation.

`RU-5-final-evidence` evidence-only actions, each identified by
`(exact source hash,evidence-manifest SHA-256)` and making no semantic code
change:

1. **A1 — correctness/platform:** focused math/RHI/interaction evidence,
   native non-Null backend lifecycle, four-platform CI, install, and consumer
   smoke.
2. **A2 — performance/visual generation:** generate the fixed full-frame
   performance/visual manifest and representative positive/negative/
   cancellation/step/gap/preview/unrelated-LOD bundle, then complete delegated
   technical review. A2 neither requests nor records human approval inside its
   reviewer disposition.
3. **A3 — release audit:** after the separate nondelegated human action, validate
   and retain its binding to the exact clean A2 identity along with provenance,
   artifact hashes, package/API, loser-removal, review-identity, and handoff
   audit against the final release gate.

Gate allocation:

- production A1 owns winner/private-copy provenance and benchmark production-
  entry parity; production A2 owns internal frame/range/cache/result and
  benchmark-consumption oracles; production A3 owns internal RHI byte/resource/
  draw parity plus native non-Null lifecycle/pixel gates;
- `STACK_PUBLIC_ACTIVATION` owns metadata/builder/registry reachability,
  public-to-internal end-to-end rendering, Qt/result/preview/cursor behavior,
  P-R1 publication/presented identity presence, exact per-series origin and
  frame-tagging/sequences, retained stale cursor rejection, exact public group/
  view/member IDs and MAIN-before-PREVIEW/member ordering, and delayed/topology-
  changed self-description with enclosing-config stale rejection. Compile/API
  tests prove the four result types have private storage, no public mutation or
  unrestricted construction, factory-enforced tagged invariants, matched const
  observers, and only the P-R1 value fields—no geometry/span/sample/resource/
  source-identity/owner/key fact. Construction/publication/lifetime tests prove
  the stack table independently owns all bytes referenced by its read-only
  slices, is populated without later renderer-side allocation, never aliases
  private backing, moves to public ownership without publication allocation or
  dangling views,
  and copies only by shared immutable public backing or complete deep-copy/
  rebase, plus normative
  API/migration/package docs and install/package consumer smoke on the same
  source hash;
  public A3 owns only tutorial/example compilation and supplementary package
  evidence;
- final-evidence A1 owns correctness/platform manifests; A2 owns performance/
  visual generation and technical review only; after clean A2, a separate
  nondelegated human approval is bound to its exact identity; A3 validates and
  retains that binding in the final release-audit manifest. Workers and
  reviewers cannot create, infer, or substitute approval. Each evidence unit
  runs the common evidence-only gate and is reviewed by its exact
  `(source hash,manifest SHA-256)` identity.

Any semantic finding returns to its named source unit, after which all affected
evidence units restart with a new source hash and manifest. Evidence units never
patch code.

Cache rules:

- READY base-composition reuse requires an identical `content_key`, stable nonzero sequences, and non-conservative/explicit access semantics;
- unstable/conservative inputs recompute within `M`, `V_limit`, and `H_limit` and record the disable reason;
- BUSY compares only `structure_key`; P-R1 STALE_BUSY copies the prior complete
  tagged presentation wholesale, exposes the current BUSY only at group level,
  and leaves current partial attempts in trace/counters. With no retained match,
  the result is `FAILED(SOURCE_BUSY)` after every required acquisition entry is
  CURRENT_OBSERVATION;
- READY replaces the retained entry; EMPTY, every `FAILED(reason)`, every
  budget/allocation failure, and structural-key change clear eligibility before
  publication. READY→BUSY→BUSY may stale, while every tested terminal/
  structural transition followed by BUSY cannot;
- D13 observed absence retires the slot; no source identity/incarnation/geometry/instance token exists and no content-derived fact enters `structure_key`;
- color/label changes do not recompose;
- boundary data key is base-content revision, LINE-membership mask, and spans;
- color, width, opacity, and pixel-snap changes update uniforms/draw state only and never upload boundary data;
- adding/removing LINE style changes boundary membership and may rebuild boundary data, not base geometry;
- changed LINE data composes once, uploads base once, and uploads padded boundary once.

Correctness gate:

- all owner-approved contract cases pass;
- stack results remain self-describing across delayed delivery and topology
  change: every group/view/member carries its structural IDs, MAIN records
  precede PREVIEW, group/member ordering is deterministic, and the enclosing
  executed-plan config identity rejects stale consumption without treating an
  ID as reuse authority;
- exhaustive result variants make every invalid payload combination
  unconstructible; post-composition numeric, resident-cap, CPU-allocation, and
  RHI-allocation failures expose no rendered range or indicator;
- negative cancellation includes intermediate cumulative range;
- VISIBLE range/render plan identity proves cancellation and every intermediate cumulative extremum cannot be clipped;
- common-domain EMPTY and hold behavior pass;
- Candidate-specific STEP behavior has no diagonal bridge;
- preview and indicator match the published rendered representation;
- main and preview LODs remain independent;
- failed/empty preview group never suppresses main and never renders partial preview;
- unordered input, failed member, custom layer, missing scalar, singleton, all-NONE, and budget failures are explicit;
- no partial member is silently dropped within one immutable topology.
- after every double addition, convert bottom/top to v1 float and require the
  converted values to be finite before any emit; ordinary finite rounding is
  accepted without exact roundtrip, while float-max positive/negative,
  overflow, and cancellation cases produce the proposed canonical disposition
  with no partial geometry.

Performance gate:

- the ratified P-D15 formulas, candidate-independent metadata-derived
  `U_limit`, checked counters, actual-upload accounting, and fixed frame
  admission pass exactly;
- exact-limit/next-operation probes prove prospective visit, output-pair, and
  upload-write checks fail before side effects, counters never exceed limits,
  and the result is `FAILED(STACK_OUTPUT_BUDGET_EXCEEDED)`;
- stack-exclusive CPU and exact requested QRhi byte instrumentation proves both
  instantaneous caps, including temporary replacement storage, on every tested
  transaction;
- the narrow exact-size owning array proves exact count/bytes and ownership
  destruction; ordinary Stage 3 vectors remain unchanged. Result-builder and
  backing counters cover stack-table construction, atomic publication, private
  `renderer_retained_stack_entry_t` geometry/resources and its one value-only
  retained presentation record exactly once despite sharing, an independently
  allocated public stack table that never aliases private backing, no
  post-install renderer-side deep-copy allocation, atomic debit when that table
  moves to GUI/public ownership, and final private renderer-reference release
  with no transfer/alias or transient uncounted escape; ordinary D9 storage and
  opaque objects remain excluded;
- structural-result probes exhaust every section/entry/member factory and
  matched/mismatched observer, prove group/view/member IDs and deterministic
  ordering survive publication/move/copy, and reject public construction or
  mutation. READY/STALE_BUSY alone expose content ID/range/indicators;
  EMPTY and every failure, including post-composition numeric, resident-cap,
  CPU-allocation, and RHI-allocation failures, expose none;
- first-frame and replacement exact-table cap/allocation faults publish the
  allocation-free `stack_result_section_t` with
  `FAILED(RESULT_STORAGE_BUDGET)`/`FAILED(RESULT_STORAGE_ALLOCATION_FAILED)`, no
  stack table/acquisition/geometry, and no surviving stack stale eligibility;
  mixed ordinary/stack fault replays preserve ordinary pixels, statuses,
  counters, planning, rendering, and result fields exactly;
- retained-old-target fit, debit-old-target replacement, configured-cap
  `FAILED(RESIDENT_BUDGET)`, in-cap CPU/QRhi
  `FAILED(RESOURCE_ALLOCATION_FAILED)`, old-target removal and stale suppression
  after every allocation/frame/output/resident-budget failure, and atomic
  complete-install tests all match P-D15 with no partial cache/geometry or
  opaque-residency claim;
- fixed reservations never exceed `FRAME_M_LIMIT`, `FRAME_V_LIMIT`, or `FRAME_H_LIMIT`; all MAIN units precede all PREVIEW units, each class orders by ascending `lowest_enabled_series_id`, previews cannot change MAIN admission, and rejected units emit no geometry. Every accepted unit receives an immutable `{M,V_limit,H_limit}` slice; low versus full actual use by one unit leaves every later unit identical, with no shared runtime balance, slack transfer, refund, or readmission;
- zero steady allocation after warm-up where cache reuse is permitted;
- stable unchanged second frame: zero composition/base/boundary upload;
- output, composition visits, snapshot bytes, alignment visits, and producer locks satisfy the Stage 3C bounded-history gate;
- GLOBAL and GLOBAL_LOD repeat the 1x/10x/100x fixed-domain history gate; a fallback scalar range scan that grows with history returns `FAILED(STACK_GLOBAL_RANGE_UNBOUNDED)` unless a separately approved bounded direct-range contract exists;
- non-stack counters are bit-for-bit unchanged;
- repeat the approved Stage 2 raw-run protocol; full-frame p50/p95/p99, GPU-finished time, bytes, draws, allocations, snapshots, producer wait, and memory satisfy its numeric rule;
- introduce a split shared-time/ordinate layout only through a later measured plan amendment if the simple one-VBO layout fails an accepted bytes/p95 target.

Visual gate:

- clean A2 first generates and technically validates positive, negative,
  cancellation, mixed STEP/LINEAR, gap, preview, and unrelated-LOD evidence;
  afterward a separate nondelegated human approval is recorded against A2's
  exact `(source hash,evidence-manifest SHA-256)` identity before any screenshot
  becomes a golden oracle. A3 retains and validates that binding; no worker or
  reviewer approval substitutes for it.

## Final release gate

Before the feature merge/release is considered complete:

1. governed style pipeline passes;
2. initialized Release build and full CTest pass;
3. shader bake/resource lifecycle passes through Qt's native non-Null backend on every supported platform; API-specific diagnostic coverage is optional;
4. dependency and `vnm_plot` Linux/Windows/macOS/FreeBSD workflows pass;
5. clean no-sibling configure/install/consumer smoke passes while fetching dependency `master`;
6. benchmark metadata names actual backend and resolved dependency commit;
7. deterministic and end-to-end performance gates pass;
8. every completed source unit has three clean xhigh reviews of its exact source
   hash and every evidence-only unit has three clean reviews of its exact
   `(source hash,manifest SHA-256)` identity, including an
   architecture/contracts/ownership/missing-primitives review; any source or
   manifest change restarted that loop, and the required nondelegated owner/
   human approval is bound to and retained against its exact evidence identity;
9. all observed failures, including recovered ones, remain in the handoff;
10. worktree contains only the intended batch changes.

## Recommended immediate next action

Batch 1A and style Batches 1B/1C are closed. Run
`RU-2.1-exact-head/A1` against the exact committed candidate and bind review to
`(source hash,CI/gate-manifest SHA-256)`. Any red causal correction becomes a
new numbered `RU-2.1-remediation/A<n>`, receives the common source gates and
three clean exact-hash reviews, and is followed by a fresh A1. Only after clean
A1 run evidence-only `RU-2.1-calibration`, retain its
`(source hash,proposal-manifest SHA-256)`, and present that exact proposal hash
to the owner. Checkpoint PASS remains impossible until explicit owner approval.
D4 remains open for private Stage 4 evidence and does not block Checkpoint 2.1.
