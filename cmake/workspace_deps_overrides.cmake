get_filename_component(_vnm_plot_workspace_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(_vnm_plot_glm_source_dir "${_vnm_plot_workspace_root}/glm-src")
if(EXISTS "${_vnm_plot_glm_source_dir}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_glm "${_vnm_plot_glm_source_dir}" CACHE PATH "" FORCE)
endif()

set(_vnm_plot_glatter_source_dir "${_vnm_plot_workspace_root}/glatter-src")
if(EXISTS "${_vnm_plot_glatter_source_dir}/include/glatter/glatter.h")
    set(GLATTER_SOURCE_DIR "${_vnm_plot_glatter_source_dir}" CACHE PATH "" FORCE)
endif()

set(_vnm_plot_freetype_source_dir "${_vnm_plot_workspace_root}/freetype-src")
if(EXISTS "${_vnm_plot_freetype_source_dir}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_freetype "${_vnm_plot_freetype_source_dir}" CACHE PATH "" FORCE)
endif()

set(_vnm_plot_msdfgen_source_dir "${_vnm_plot_workspace_root}/msdfgen-src")
if(EXISTS "${_vnm_plot_msdfgen_source_dir}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_msdfgen "${_vnm_plot_msdfgen_source_dir}" CACHE PATH "" FORCE)
endif()

unset(_vnm_plot_glm_source_dir)
unset(_vnm_plot_glatter_source_dir)
unset(_vnm_plot_freetype_source_dir)
unset(_vnm_plot_msdfgen_source_dir)
unset(_vnm_plot_workspace_root)
