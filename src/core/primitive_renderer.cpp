#include <vnm_plot/core/primitive_renderer.h>
#include <vnm_plot/core/asset_loader.h>
#include <vnm_plot/core/constants.h>
#include <vnm_plot/core/plot_config.h>

#include <glatter/glatter.h>
#include <glm/gtc/type_ptr.hpp>

namespace vnm::plot {

Primitive_renderer::Primitive_renderer() = default;
Primitive_renderer::~Primitive_renderer() = default;

void Primitive_renderer::set_log_callback(GL_program::LogCallback callback)
{
    m_log_error = std::move(callback);
}

bool Primitive_renderer::initialize(Asset_loader& asset_loader)
{
    if (m_initialized) {
        return true;
    }

    // Load rect shader sources
    auto rect_sources = asset_loader.load_shader("shaders/generic_rect");
    if (!rect_sources) {
        if (m_log_error) {
            m_log_error("Failed to load generic_rect shader sources");
        }
        return false;
    }

    // Create rect shader program
    m_sp_rects = create_gl_program(
        rect_sources->vertex,
        rect_sources->geometry,
        rect_sources->fragment,
        m_log_error);

    if (!m_sp_rects) {
        return false;
    }

    // Create rects VAO/VBO
    glGenVertexArrays(1, &m_rects_pipe.vao);
    glGenBuffers(1, &m_rects_pipe.vbo);

    glBindVertexArray(m_rects_pipe.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_rects_pipe.vbo);

    const GLsizeiptr initial_bytes = detail::k_rect_initial_quads * static_cast<GLsizeiptr>(sizeof(rect_vertex_t));
    glBufferData(GL_ARRAY_BUFFER, initial_bytes, nullptr, GL_STREAM_DRAW);
    m_rects_pipe.capacity_bytes = static_cast<size_t>(initial_bytes);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(rect_vertex_t),
                          reinterpret_cast<void*>(offsetof(rect_vertex_t, color)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(rect_vertex_t),
                          reinterpret_cast<void*>(offsetof(rect_vertex_t, rect_coords)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Load grid shader sources
    auto grid_sources = asset_loader.load_shader("shaders/grid_quad");
    if (!grid_sources) {
        if (m_log_error) {
            m_log_error("Failed to load grid_quad shader sources");
        }
        cleanup_gl_resources();
        return false;
    }

    // Create grid shader program
    m_sp_grid = create_gl_program(
        grid_sources->vertex,
        grid_sources->geometry,  // May be empty
        grid_sources->fragment,
        m_log_error);

    if (!m_sp_grid) {
        cleanup_gl_resources();
        return false;
    }

    // Create grid VAO/VBO (static unit quad)
    glGenVertexArrays(1, &m_grid_quad_pipe.vao);
    glGenBuffers(1, &m_grid_quad_pipe.vbo);

    glBindVertexArray(m_grid_quad_pipe.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_grid_quad_pipe.vbo);

    const GLfloat quad[] = {
        -1.f, -1.f,
         1.f, -1.f,
        -1.f,  1.f,
         1.f,  1.f
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_initialized = true;
    return true;
}

void Primitive_renderer::cleanup_gl_resources()
{
    if (m_rects_pipe.vao != 0) {
        glDeleteVertexArrays(1, &m_rects_pipe.vao);
    }
    if (m_rects_pipe.vbo != 0) {
        glDeleteBuffers(1, &m_rects_pipe.vbo);
    }
    if (m_grid_quad_pipe.vao != 0) {
        glDeleteVertexArrays(1, &m_grid_quad_pipe.vao);
    }
    if (m_grid_quad_pipe.vbo != 0) {
        glDeleteBuffers(1, &m_grid_quad_pipe.vbo);
    }

    m_rects_pipe     = {};
    m_grid_quad_pipe = {};

    m_sp_rects.reset();
    m_sp_grid.reset();
    m_cpu_buffer.clear();
    m_initialized = false;
}

void Primitive_renderer::batch_rect(const glm::vec4& color, const glm::vec4& rect_coords)
{
    if (m_cpu_buffer.size() == m_cpu_buffer.capacity()) {
        m_cpu_buffer.reserve(m_cpu_buffer.size() + detail::k_rect_initial_quads);
    }
    m_cpu_buffer.push_back({color, rect_coords});
}

void Primitive_renderer::flush_rects(const glm::mat4& pmv)
{
    VNM_PLOT_PROFILE_SCOPE(m_profiler, "prims.flush_rects");

    if (m_cpu_buffer.empty() || !m_sp_rects || m_rects_pipe.vbo == 0 || m_rects_pipe.vao == 0) {
        m_cpu_buffer.clear();
        return;
    }

    const GLsizeiptr bytes_needed = static_cast<GLsizeiptr>(m_cpu_buffer.size() * sizeof(rect_vertex_t));

    glBindBuffer(GL_ARRAY_BUFFER, m_rects_pipe.vbo);

    if (bytes_needed > static_cast<GLsizeiptr>(m_rects_pipe.capacity_bytes)) {
        glBufferData(GL_ARRAY_BUFFER, bytes_needed, m_cpu_buffer.data(), GL_STREAM_DRAW);
        m_rects_pipe.capacity_bytes = static_cast<size_t>(bytes_needed);
    }
    else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes_needed, m_cpu_buffer.data());
    }

    m_sp_rects->bind();
    glUniformMatrix4fv(
        m_sp_rects->uniform_location("pmv"),
        1, GL_FALSE, glm::value_ptr(pmv));

    glBindVertexArray(m_rects_pipe.vao);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(m_cpu_buffer.size()));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_cpu_buffer.clear();
}

void Primitive_renderer::draw_grid_shader(
    const glm::vec2& origin,
    const glm::vec2& size,
    const glm::vec4& color,
    const grid_layer_params_t& vertical_levels,
    const grid_layer_params_t& horizontal_levels)
{
    VNM_PLOT_PROFILE_SCOPE(m_profiler, "prims.draw_grid");

    if (!m_sp_grid || m_grid_quad_pipe.vao == 0 || m_grid_quad_pipe.vbo == 0) {
        return;
    }
    if (vertical_levels.count <= 0 && horizontal_levels.count <= 0) {
        return;
    }

    m_sp_grid->bind();

    const GLuint pid = m_sp_grid->program_id();

    glUniform2f(glGetUniformLocation(pid, "plot_size_px"), size.x, size.y);
    glUniform2f(glGetUniformLocation(pid, "region_origin_px"), origin.x, origin.y);
    glUniform4f(glGetUniformLocation(pid, "grid_color"), color.r, color.g, color.b, color.a);

    const int max_levels = grid_layer_params_t::k_max_levels;

    glUniform1i(glGetUniformLocation(pid, "v_count"), vertical_levels.count);
    glUniform1fv(glGetUniformLocation(pid, "v_spacing_px"), max_levels, vertical_levels.spacing_px);
    glUniform1fv(glGetUniformLocation(pid, "v_start_px"), max_levels, vertical_levels.start_px);
    glUniform1fv(glGetUniformLocation(pid, "v_alpha"), max_levels, vertical_levels.alpha);
    glUniform1fv(glGetUniformLocation(pid, "v_thickness_px"), max_levels, vertical_levels.thickness_px);

    glUniform1i(glGetUniformLocation(pid, "t_count"), horizontal_levels.count);
    glUniform1fv(glGetUniformLocation(pid, "t_spacing_px"), max_levels, horizontal_levels.spacing_px);
    glUniform1fv(glGetUniformLocation(pid, "t_start_px"), max_levels, horizontal_levels.start_px);
    glUniform1fv(glGetUniformLocation(pid, "t_alpha"), max_levels, horizontal_levels.alpha);
    glUniform1fv(glGetUniformLocation(pid, "t_thickness_px"), max_levels, horizontal_levels.thickness_px);

    glEnable(GL_SCISSOR_TEST);
    glScissor(
        static_cast<GLint>(lround(origin.x)),
        static_cast<GLint>(lround(origin.y)),
        static_cast<GLsizei>(lround(size.x)),
        static_cast<GLsizei>(lround(size.y)));

    glBindVertexArray(m_grid_quad_pipe.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glDisable(GL_SCISSOR_TEST);
}

} // namespace vnm::plot
