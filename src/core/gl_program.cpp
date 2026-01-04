#include <vnm_plot/core/gl_program.h>

#include <glatter/glatter.h>

#include <atomic>
#include <memory>
#include <utility>

namespace vnm::plot::core {

namespace {
std::atomic<bool> g_gl_initialized{false};
} // anonymous namespace

bool init_gl()
{
    // Allow idempotent calls, but only actually initialize once
    if (g_gl_initialized.load(std::memory_order_acquire)) {
        return true;
    }

    // Initialize glatter extension loading
    // This must be called after an OpenGL context is current
    // glatter_get_extension_support_GL() populates function pointers
    // and returns a status struct (we just need to call it)
    (void)glatter_get_extension_support_GL();

    g_gl_initialized.store(true, std::memory_order_release);
    return true;
}

GL_program::GL_program() = default;

GL_program::~GL_program()
{
    destroy();
}

GL_program::GL_program(GL_program&& other) noexcept
    : m_program_id(other.m_program_id)
    , m_vertex_shader(other.m_vertex_shader)
    , m_geometry_shader(other.m_geometry_shader)
    , m_fragment_shader(other.m_fragment_shader)
    , m_linked(other.m_linked)
    , m_log_callback(std::move(other.m_log_callback))
{
    other.m_program_id = 0;
    other.m_vertex_shader = 0;
    other.m_geometry_shader = 0;
    other.m_fragment_shader = 0;
    other.m_linked = false;
}

GL_program& GL_program::operator=(GL_program&& other) noexcept
{
    if (this != &other) {
        destroy();

        m_program_id = other.m_program_id;
        m_vertex_shader = other.m_vertex_shader;
        m_geometry_shader = other.m_geometry_shader;
        m_fragment_shader = other.m_fragment_shader;
        m_linked = other.m_linked;
        m_log_callback = std::move(other.m_log_callback);

        other.m_program_id = 0;
        other.m_vertex_shader = 0;
        other.m_geometry_shader = 0;
        other.m_fragment_shader = 0;
        other.m_linked = false;
    }
    return *this;
}

void GL_program::set_log_callback(LogCallback callback)
{
    m_log_callback = std::move(callback);
}

void GL_program::log_error(const std::string& message) const
{
    if (m_log_callback) {
        m_log_callback(message);
    }
}

bool GL_program::compile_shader(GLuint shader_type, std::string_view source, GLuint& out_shader)
{
    out_shader = glCreateShader(shader_type);
    if (out_shader == 0) {
        log_error("Failed to create shader object");
        return false;
    }

    const char* source_ptr = source.data();
    const GLint source_len = static_cast<GLint>(source.size());
    glShaderSource(out_shader, 1, &source_ptr, &source_len);
    glCompileShader(out_shader);

    GLint compile_status = 0;
    glGetShaderiv(out_shader, GL_COMPILE_STATUS, &compile_status);

    if (compile_status == GL_FALSE) {
        GLint log_length = 0;
        glGetShaderiv(out_shader, GL_INFO_LOG_LENGTH, &log_length);

        std::string info_log(static_cast<size_t>(log_length), '\0');
        glGetShaderInfoLog(out_shader, log_length, nullptr, info_log.data());

        const char* type_name = "Unknown";
        switch (shader_type) {
            case GL_VERTEX_SHADER:   type_name = "Vertex"; break;
            case GL_GEOMETRY_SHADER: type_name = "Geometry"; break;
            case GL_FRAGMENT_SHADER: type_name = "Fragment"; break;
        }

        log_error(std::string(type_name) + " shader compilation failed: " + info_log);

        glDeleteShader(out_shader);
        out_shader = 0;
        return false;
    }

    return true;
}

bool GL_program::add_vertex_shader(std::string_view source)
{
    if (m_vertex_shader != 0) {
        log_error("Vertex shader already attached");
        return false;
    }
    return compile_shader(GL_VERTEX_SHADER, source, m_vertex_shader);
}

bool GL_program::add_geometry_shader(std::string_view source)
{
    if (m_geometry_shader != 0) {
        log_error("Geometry shader already attached");
        return false;
    }
    return compile_shader(GL_GEOMETRY_SHADER, source, m_geometry_shader);
}

bool GL_program::add_fragment_shader(std::string_view source)
{
    if (m_fragment_shader != 0) {
        log_error("Fragment shader already attached");
        return false;
    }
    return compile_shader(GL_FRAGMENT_SHADER, source, m_fragment_shader);
}

bool GL_program::link()
{
    if (m_linked) {
        return true;
    }

    if (m_vertex_shader == 0 || m_fragment_shader == 0) {
        log_error("Cannot link: vertex and fragment shaders are required");
        return false;
    }

    m_program_id = glCreateProgram();
    if (m_program_id == 0) {
        log_error("Failed to create program object");
        return false;
    }

    glAttachShader(m_program_id, m_vertex_shader);
    glAttachShader(m_program_id, m_fragment_shader);
    if (m_geometry_shader != 0) {
        glAttachShader(m_program_id, m_geometry_shader);
    }

    glLinkProgram(m_program_id);

    GLint link_status = 0;
    glGetProgramiv(m_program_id, GL_LINK_STATUS, &link_status);

    if (link_status == GL_FALSE) {
        GLint log_length = 0;
        glGetProgramiv(m_program_id, GL_INFO_LOG_LENGTH, &log_length);

        std::string info_log(static_cast<size_t>(log_length), '\0');
        glGetProgramInfoLog(m_program_id, log_length, nullptr, info_log.data());

        log_error("Program linking failed: " + info_log);

        glDeleteProgram(m_program_id);
        m_program_id = 0;
        return false;
    }

    // Shaders can be detached and deleted after linking
    glDetachShader(m_program_id, m_vertex_shader);
    glDeleteShader(m_vertex_shader);
    m_vertex_shader = 0;

    glDetachShader(m_program_id, m_fragment_shader);
    glDeleteShader(m_fragment_shader);
    m_fragment_shader = 0;

    if (m_geometry_shader != 0) {
        glDetachShader(m_program_id, m_geometry_shader);
        glDeleteShader(m_geometry_shader);
        m_geometry_shader = 0;
    }

    m_linked = true;
    return true;
}

void GL_program::bind()
{
    if (m_linked && m_program_id != 0) {
        glUseProgram(m_program_id);
    }
}

void GL_program::unbind()
{
    glUseProgram(0);
}

GLint GL_program::uniform_location(const char* name) const
{
    if (!m_linked || m_program_id == 0) {
        return -1;
    }
    return glGetUniformLocation(m_program_id, name);
}

void GL_program::destroy()
{
    if (m_vertex_shader != 0) {
        glDeleteShader(m_vertex_shader);
        m_vertex_shader = 0;
    }
    if (m_geometry_shader != 0) {
        glDeleteShader(m_geometry_shader);
        m_geometry_shader = 0;
    }
    if (m_fragment_shader != 0) {
        glDeleteShader(m_fragment_shader);
        m_fragment_shader = 0;
    }
    if (m_program_id != 0) {
        glDeleteProgram(m_program_id);
        m_program_id = 0;
    }
    m_linked = false;
}

// -----------------------------------------------------------------------------
// Helper function
// -----------------------------------------------------------------------------

std::unique_ptr<GL_program> create_gl_program(
    std::string_view vert_source,
    std::string_view geom_source,
    std::string_view frag_source,
    const GL_program::LogCallback& log_error)
{
    auto program = std::make_unique<GL_program>();
    program->set_log_callback(log_error);

    if (!program->add_vertex_shader(vert_source)) {
        return nullptr;
    }

    if (!geom_source.empty() && !program->add_geometry_shader(geom_source)) {
        return nullptr;
    }

    if (!program->add_fragment_shader(frag_source)) {
        return nullptr;
    }

    if (!program->link()) {
        return nullptr;
    }

    return program;
}

} // namespace vnm::plot::core
