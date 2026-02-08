#pragma once

// VNM Plot Library - Default Shader Sets
// Provides default shader mappings for known sample layouts.

#include "types.h"

namespace vnm::plot {

const shader_set_t& default_shader_for_layout(uint64_t layout_key, Display_style style);

} // namespace vnm::plot
