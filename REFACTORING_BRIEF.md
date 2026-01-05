Refactoring Brief: vnm_plot

Goals
- Reduce duplication between core and Qt wrapper layers without behavior changes.
- Simplify maintenance by consolidating shared algorithms/utilities.
- Keep Qt wrapper optional (core must remain Qt-free).

Non-Goals
- No API breaking changes unless explicitly approved.
- No behavior changes to rendering, LOD selection, or input handling unless called out.

Constraints
- Preserve Qt-free core build (VNM_PLOT_BUILD_QT option).
- Keep QML module packaging via vnm_plot.qrc.
- Prefer incremental refactors; avoid large rewrites.

Current Redundancies (validated)
- Parallel type systems: data snapshots, data sources, series types, and display/shader/colormap types exist in both core and wrapper. This forces adapters in src/plot_renderer.cpp.
  Files: include/vnm_plot/data_source.h, include/vnm_plot/renderers/series_renderer.h (wrapper series_data_t), include/vnm_plot/plot_types.h, include/vnm_plot/core/data_types.h, src/plot_renderer.cpp
- Duplicate glyph tables in font renderer (same UTF-8 strings in two functions).
  File: src/core/font_renderer.cpp
- Duplicate LOD scale vector generation (two codepaths).
  Files: src/plot_renderer.cpp, src/core/series_renderer.cpp
- Redundant input code in PlotIndicator (internal MouseArea while interaction feeds it).
  Files: qml/VnmPlot/PlotIndicator.qml, qml/VnmPlot/PlotView.qml
- plot_algo.h vs core/algo.h: core/algo.h duplicates a subset of plot_algo.h functions (format_axis_fixed_or_int, circular_index, get_shift, build_time_steps_covering, find_time_step_start_index, min_v_span_for).
  Files: include/vnm_plot/plot_algo.h, include/vnm_plot/core/algo.h
- Repeated palette selection (dark_mode ? Color_palette::dark() : Color_palette::light()) in renderer code; consider caching once per frame.
  Files: src/plot_renderer.cpp, src/core/chrome_renderer.cpp

Near-Redundancies (require behavioral decisions)
- Multiple binary search implementations (not identical).
  Files: src/plot_renderer.cpp, src/core/series_renderer.cpp, include/vnm_plot/plot_algo.h, src/plot_widget.cpp
- Min/max scanning loops (similar but not identical).
  Files: src/plot_renderer.cpp, src/core/series_renderer.cpp, src/plot_widget.cpp
- LOD level selection algorithms differ between plot_renderer.cpp and series_renderer.cpp; not safe to merge without picking a single behavior.
  Files: src/plot_renderer.cpp, src/core/series_renderer.cpp

Proposed Phased Refactor

Phase 1 (low-risk cleanup, minimal behavior risk)
- Remove redundant MouseArea in qml/VnmPlot/PlotIndicator.qml; make it a passive renderer driven by PlotInteractionItem signals.
- Deduplicate glyph tables: derive glyph_seed_string() from glyph_codepoints() in src/core/font_renderer.cpp.
- Factor LOD scales computation into a single helper reused by both renderers (or add caching inside series state). Keep behavior identical.

Phase 2 (type unification)
- Choose one canonical set of types in core:
  - core::data_snapshot_t, core::snapshot_result_t, core::Data_source
  - core::Display_style, core::shader_set_t, core::colormap_config_t
  - core::series_data_t and core::Data_access_policy
- Re-export with using declarations at vnm::plot level for wrapper compatibility.
- Remove or alias wrapper duplicates.
- Delete Data_source_adapter and to_core_* functions in src/plot_renderer.cpp.

Phase 3 (algorithm consolidation)
Note: Prefer after Phase 2 to avoid duplicating changes in both core and wrapper type systems.
- Provide a single binary-search utility with explicit preconditions (ascending vs descending) and migrate call sites gradually.
- Create a small min/max helper for sample scans only where semantics match.
- Reconcile or explicitly keep the two choose_level_from_base_pps implementations. If merging, document the intended behavior and update tests.

Files to Touch (by phase)
- Phase 1: qml/VnmPlot/PlotIndicator.qml, qml/VnmPlot/PlotView.qml, src/core/font_renderer.cpp, src/plot_renderer.cpp, src/core/series_renderer.cpp
- Phase 2: include/vnm_plot/core/data_types.h, include/vnm_plot/data_source.h, include/vnm_plot/plot_types.h, include/vnm_plot/renderers/series_renderer.h, src/plot_renderer.cpp
- Phase 3: include/vnm_plot/plot_algo.h, include/vnm_plot/core/algo.h, src/plot_renderer.cpp, src/core/series_renderer.cpp, src/plot_widget.cpp

Estimates (rough, optional)
- Phase 1: ~40-120 lines (indicator MouseArea + glyph table dedupe + LOD scale helper).
- Phase 2: ~100-300 lines (adapter removal, type aliasing, duplicate types removed).
- Phase 3: highly variable; depends on which algorithms are standardized.

Decisions Required (before Phase 2/3)
- Should vnm::plot alias core types or keep separate types for ABI/backward compatibility reasons?
- Which LOD selection algorithm is canonical (plot vs series renderer)?
- Must binary search support descending timestamps everywhere?

Validation / Tests
- Build core-only target and Qt wrapper target.
- Run function_plotter example to confirm: pan/zoom, indicator, preview interactions.
- Spot-check LOD behavior on large datasets.

Success Criteria
- No behavior regressions in example apps.
- Data_source_adapter removed (if Phase 2 completed).
- Clear ownership: core types live in core/, wrapper re-exports or uses aliases.
- Reduced QML input code and duplicated glyph tables.
