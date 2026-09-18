#include "storage/journal.hpp"
#include "util/time_utils.hpp"
#include <fstream>

namespace tt {

bool append_journal_entry(const std::filesystem::path& path, std::string_view text,
                           std::string& error) {
    std::ofstream out(path, std::ios::app);
    if (!out) {
        error = "cannot open " + path.string() + " for append";
        return false;
    }
    out << "[" << time_utils::format(time_utils::now()) << "] " << text << '\n';
    if (!out) {
        error = "write failed for " + path.string();
        return false;
    }
    return true;
}

}  // namespace tt