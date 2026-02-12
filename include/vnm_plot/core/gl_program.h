#pragma once

// VNM Plot Library - GL Program
// Qt-free OpenGL shader program wrapper.

#include "types.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

// Forward declare GL types to avoid including GL headers in this header
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;

namespace vnm::plot {

// -----------------------------------------------------------------------------
// OpenGL Initialization
// -----------------------------------------------------------------------------

/// Initialize OpenGL extension loading via glatter.
/// MUST be called once after an OpenGL context is current and before using
/// any vnm_plot_core rendering functions.
///
/// Typical usage:
/// - In Qt: Call from QQuickFramebufferObject::Renderer::render() on first frame
/// - In GLFW: Call after glfwMakeContextCurrent() and before rendering
///
/// Returns true on success.
/// Thread-safety: Must be called from the thread with the active GL context.
[[nodiscard]] bool init_gl();

// -----------------------------------------------------------------------------
// GL_program
// -----------------------------------------------------------------------------
// A Qt-free wrapper for OpenGL shader programs.
// Manages shader compilation, linking, and uniform access.
class GL_program
{
public:
    // Log callback for errors and warnings
    using Log_callback = std::function<void(const std::string&)>;

    GL_program();
    ~GL_program();

    // Non-copyable, movable
    GL_program(const GL_program&) = delete;
    GL_program& operator=(const GL_program&) = delete;
    GL_program(GL_program&& other) noexcept;
    GL_program& operator=(GL_program&& other) noexcept;

    // Set log callback for shader compilation errors
    void set_log_callback(Log_callback callback);

    // Compile and attach shaders from source strings
    // Returns true on success.
    bool add_vertex_shader(std::string_view source);
    bool add_geometry_shader(std::string_view source);
    bool add_fragment_shader(std::string_view source);

    // Link the program after all shaders are attached
    // Returns true on success.
    bool link();

    // Bind/unbind the program for rendering
    void bind();
    static void unbind();

    // Get the OpenGL program ID (for direct GL calls)
    [[nodiscard]] GLuint program_id() const noexcept { return m_program_id; }

    // Check if program is valid and linked
    [[nodiscard]] bool is_valid() const noexcept { return m_linked; }

    // Get uniform location (cached internally if needed)
    [[nodiscard]] GLint uniform_location(const char* name) const;

    // Delete the program and all attached shaders
    void destroy();

private:
    bool compile_shader(GLuint shader_type, std::string_view source, GLuint& out_shader);
    void log_error(const std::string& message) const;

    GLuint m_program_id = 0;
    GLuint m_vertex_shader = 0;
    GLuint m_geometry_shader = 0;
    GLuint m_fragment_shader = 0;
    bool m_linked = false;

    Log_callback m_log_callback;
    mutable std::unordered_map<std::string, GLint> m_uniform_cache;
};

// -----------------------------------------------------------------------------
// Helper: Load and create a GL_program from shader sources
// -----------------------------------------------------------------------------
// Returns nullptr on failure.
// geom_source can be empty if no geometry shader is used.
[[nodiscard]]
std::unique_ptr<GL_program> create_gl_program(
    std::string_view vert_source,
    std::string_view geom_source,
    std::string_view frag_source,
    const GL_program::Log_callback& log_error = nullptr);

} // namespace vnm::plot
