#pragma once

// VNM Plot Library - Atomic File Publication
// Publishes a file only when it was written whole.

#include <filesystem>
#include <functional>
#include <ostream>

namespace vnm::plot::detail {

// Runs write_body against a uniquely named temporary in the destination
// directory and replaces path with that temporary only when write_body reported
// success and the stream flushed and closed without error. On any failure the
// destination keeps its previous contents and the temporary is removed, so a
// reader never observes a half-written file and a losing writer leaves no
// orphan behind. Returns true only when the destination was replaced.
[[nodiscard]] bool write_file_atomically(
    const std::filesystem::path&                path,
    const std::function<bool(std::ostream&)>&   write_body);

} // namespace vnm::plot::detail
