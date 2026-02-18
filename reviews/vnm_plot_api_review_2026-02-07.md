# vnm_plot API Review (2026-02-07)

## Scope
Reviewed public headers in `include/vnm_plot/core` and `include/vnm_plot/qt`, plus the examples in `examples/hello_plot`, `examples/preview_config`, `examples/function_plotter`, and `examples/standalone_glfw`. I also looked at a downstream integration codebase to gauge real-world friction.

## Current State Summary
To plot a series, a user typically:
- Creates a `Data_source` (e.g., `Vector_data_source` or a custom subclass).
- Builds a `Data_access_policy` with `void*` callbacks for timestamp/value/range, manually sets `sample_stride`, assigns a `layout_key`, and provides `setup_vertex_attributes`.
- Chooses shader assets (`shader_set` or per-style `shaders`) with explicit asset paths.
- Populates a `series_data_t` and calls `Plot_widget::add_series(id, series)`.
- Configures view/ranges via multiple calls (`set_t_range`, `set_available_t_range`, `set_v_auto`, `set_v_range`) and preview settings.

This pattern is visible across the Qt examples and the core-only GLFW example, and downstream code often adds wrappers to reduce the repeated setup.

## API Improvement Opportunities
1. **Auto-generate `Data_access_policy` from a sample type**
Current: every example and downstream policy factory repeats the same `void*` casts and offset math for timestamps, values, ranges, and vertex attributes. `make_function_sample_policy()` exists but still leaves `setup_vertex_attributes` and shader selection to the user.
What could change: provide a template helper such as `make_access_policy<Sample>(member pointers...)` or a traits specialization that auto-fills `get_timestamp/get_value/get_range`, `sample_stride`, `layout_key`, and `setup_vertex_attributes` for the default shader layout. This removes most boilerplate and eliminates per-project copies of the same code.

2. **Hide `void*` casts from user code**
Current: the public API requires users to cast `const void*` to their sample type in each lambda; this is repeated across all examples and downstream policies.
What could change: wrap the type-erased layer inside a typed `Data_access_policy_t<Sample>` and provide `.erase()` to produce the library’s `Data_access_policy`. This keeps the renderer interface while removing casts from user code.

3. **Provide default shader sets and vertex layout for built-in sample types**
Current: users must specify shader asset paths even for standard plotting. The Qt examples and `function_plotter` manually set `shader_set` or `shaders` for each series, and the GLFW example must specify file paths directly.
What could change: if `shader_set` or `shaders` are empty, choose defaults based on `Display_style` and the layout key (for example, the built-in function sample layout). This lets most users skip shader configuration entirely while still supporting overrides for custom shaders.

4. **Replace non-owning `shared_ptr` with an explicit non-owning API**
Current: multiple examples wrap member-owned data sources in `shared_ptr` with a no-op deleter. This is a dangling-pointer risk if the owner is destroyed.
What could change: allow `series_data_t` to hold a raw pointer or a dedicated non-owning wrapper (`observer_ptr`), or provide a `series_data_t::set_data_source_ref(Data_source&)` helper. This makes lifetime intent explicit and avoids the no-op deleter pattern.

5. **Remove duplicated series IDs**
Current: `series_data_t` has an `id`, but `Plot_widget::add_series` also takes an `id` parameter and uses it as the map key. Mismatches are easy to create and hard to diagnose.
What could change: remove the `id` argument and use `series->id`, or remove `series_data_t::id` and return an assigned ID from `add_series`. Either way, there should be one source of truth.

6. **Make `sample_stride` a single source of truth**
Current: `Data_source::sample_stride()` and `Data_access_policy::sample_stride` both exist and are checked against each other at runtime (including in preview handling). This creates another place for subtle mismatch bugs.
What could change: store stride only on the data source and have access policies use it implicitly, or have the typed policy generator set it from the sample type and validate once on binding.

7. **Create a `Series_builder` or factory**
Current: constructing a series requires many assignments (style, color, data source, access policy, shaders, preview config). The `function_plotter` example shows this clearly.
What could change: a small builder or `make_series()` helper that enforces required fields and provides fluent optional configuration would shorten sample code and reduce missing-field errors.

8. **Simplify color specification**
Current: colors are always `glm::vec4` with 0–1 components; every example repeats literal RGBA values.
What could change: allow common inputs such as `rgb(40, 118, 178)`, hex `0x2876B2`, or named palette values, with conversion to `glm::vec4` internally.

9. **Make `frame_context_t` construction robust**
Current: core users must build `frame_context_t` with a long positional aggregate in `standalone_glfw`. This is brittle and hard to read.
What could change: add a builder or a named-parameter factory function that fills defaults and only requires the inputs that actually vary per frame.

10. **Unify `Plot_config` and `Render_config`**
Current: the core renderer uses `Render_config` while the Qt wrapper uses `Plot_config`, and `Plot_renderer` copies overlapping fields every frame.
What could change: unify into a single config type (or make `Render_config` a view of `Plot_config`). This eliminates duplication and reduces the cognitive load between Qt and core usage.

11. **Batch initial view/range setup**
Current: users must call a chain of setters (`set_t_range`, `set_available_t_range`, `set_v_auto`, `set_v_range`, preview height controls) to get a sensible initial view.
What could change: add a `Plot_widget::set_view()` struct or fold initial ranges into `Plot_config` so users can set view state once.

12. **Provide a high-level core facade**
Current: the core example manually orchestrates `Asset_loader`, `Primitive_renderer`, `Series_renderer`, and `Chrome_renderer`, and builds frame layout and context manually.
What could change: provide a `Plot_core` (or similar) that owns these and exposes a simple `render(width, height, series, config)` API. Power users can still use the low-level renderers.

13. **Make series snapshots read-only**
Current: `get_series_snapshot()` returns `shared_ptr<series_data_t>` which can be mutated outside the render thread.
What could change: return `shared_ptr<const series_data_t>` or a read-only snapshot type to prevent accidental data races.

14. **Replace magic `layout_key` numbers with derived keys**
Current: examples manually assign hex layout keys (for example, `0x1001`, `0x2001`, `0x3001`) without validation.
What could change: compute `layout_key` from a `Layout_descriptor` that lists attribute types and offsets, or allow a user-provided string that is hashed once. This makes uniqueness and intent explicit.

## Downstream Viewpoint
Downstream wrappers often centralize shader selection and vertex layouts in separate policy factories. That pattern highlights the cost of the current API: repeated boilerplate and duplicated setup that could be handled directly by vnm_plot.
The highest-value changes are the ones that eliminate policy boilerplate, shader-path plumbing, and non-owning `shared_ptr` patterns. A typed policy builder and default shader or layout registry would immediately reduce setup size and fragility while also simplifying the public examples.

## Suggested Priority
1. Typed policy builder with generated vertex layout and `layout_key`.
2. Default shader sets for standard layouts and styles.
3. Remove duplicated IDs and clarify data source ownership.
4. Unify config types and add view/range batching.
5. High-level core facade.
