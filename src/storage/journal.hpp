#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace tt {

// Appends one timestamped line to a plain-text journal file, creating it if
// needed. Returns false and sets `error` on failure.
bool append_journal_entry(const std::filesystem::path& path, std::string_view text,
                           std::string& error);

}  // namespace tt