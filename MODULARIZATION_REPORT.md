# vnm_plot Modularization Report

## Context
- Goal: split into a Qt-free core and a Qt backend.
- Decision: MSDF text rendering stays in core.
- Decision: API stability is not a constraint for this refactor.

## Validated Proposal Points
- Core/Qt split is the right direction for portability and testing.
- Dependency pinning is needed (glatter is currently on master).
- Standalone GLFW example is a strong validation harness.
- Threading model assumptions match current behavior (render thread has active GL context).

## Corrections to the Proposal (Based on Repo Reality)
1) Core dependencies are broader than listed.
   - Required: C++20, OpenGL 4.3+, GLM, glatter, FreeType, msdfgen.
2) Qt coupling is deeper than "plot.cpp".
   - Qt types appear in layout, text, and shader paths:
     - `include/vnm_plot/layout/layout_calculator.h`
     - `src/layout/layout_calculator.cpp`
     - `src/renderers/primitive_renderer.cpp`
     - `src/renderers/series_renderer.cpp`
     - `src/renderers/text_renderer.cpp`
     - `src/renderers/font_renderer.cpp`
3) glatter usage is not header-only.
   - Current build compiles `glatter.c` and calls `glatter_get_extension_support_GL()`.
   - Core should keep an explicit `init_gl()` entry point after a context is current.
4) Data access is duplicated.
   - `Data_access_policy` exists, but `series_data_t` duplicates accessors.
   - Core should unify on a single data access interface.
5) Shader/font loading relies on Qt resources.
   - `:/vnm_plot/...` requires a new non-Qt asset strategy in core.

## Additional Observation
- README claims OpenGL 3.3+, but runtime checks require 4.3 + `GL_ARB_gpu_shader_int64`.
  Align docs with actual requirements during refactor.

## Recommended Architecture
### vnm_plot_core (Qt-free)
- Responsibilities:
  - Renderers (series, chrome, primitives, text, font).
  - Layout calculation and caches.
  - GL program compilation and resource management.
  - Data access abstraction (single interface).
- Dependencies: OpenGL, glatter, GLM, FreeType, msdfgen, standard C++.
- Asset loading:
  - Provide embedded defaults (shaders + monospace font).
  - Allow optional filesystem overrides for development/debug.

### vnm_plot_qt (Qt backend)
- Responsibilities:
  - `QQuickFramebufferObject` wrapper.
  - UI thread to render thread sync.
  - Qt-specific conversions (colors, QML bindings).
  - Expose a friendly QML API.

## Implementation Phases
### Phase 0 - Dependency Hygiene
- Pin glatter to a tag/commit in `CMakeLists.txt`.
- Check other FetchContent deps for floating tags.

### Phase 1 - Core Abstractions
- Add a Qt-free GL program wrapper (replaces `QOpenGLShaderProgram`).
- Add asset loader (embedded + optional file override).
- Add UTF-8 utilities (replace `QString::fromUtf8().toUcs4()`).
- Add cross-platform cache-dir helper (or plan to drop disk cache).

### Phase 2 - Renderer Migration
- Move renderers to core and remove Qt types:
  - `primitive_renderer`
  - `series_renderer`
  - `chrome_renderer`
  - `text_renderer`

### Phase 3 - Font System Migration
- Replace Qt crypto, file IO, and paths in `font_renderer`.
- Keep FreeType/msdfgen usage unchanged otherwise.

### Phase 4 - Layout Migration
- Replace `QString`/`QByteArray` in `layout_calculator`.
- Use `std::string` + UTF-8 utilities in core.

### Phase 5 - Data Access Unification
- Make `series_data_t` compose or wrap `Data_access_policy`.
- Remove duplicated accessors in renderer paths.

### Phase 6 - Core Engine + Qt Wrapper
- Create a Qt-free `Plot_engine` that takes a render snapshot.
- Refactor current `Plot_renderer` to own and feed `Plot_engine`.

### Phase 7 - Validation
- Add `examples/standalone_glfw`.
- Verify behavior against Qt demo (visual and functional).

## Decisions
1) Asset strategy:
   - Embedded defaults with optional filesystem override for development.
2) MSDF disk cache:
   - Enabled by default; provide an option to disable.
3) Target platforms:
   - If macOS is needed, confirm OpenGL support and cache paths early.

## Immediate Next Steps
1) Pin glatter in `CMakeLists.txt`.
2) Introduce the Qt-free GL program wrapper in core.
