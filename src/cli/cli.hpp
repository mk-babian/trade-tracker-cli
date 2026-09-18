#pragma once

#include "storage/repository.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tt::cli {

// Entry point: parse argv, dispatch to the appropriate command, and return a
// process exit code.
int run(int argc, char** argv);

// Individual commands. Each returns true on success.
bool cmd_add(Repository& repo, const std::vector<std::string_view>& args);
bool cmd_close(Repository& repo, std::uint64_t id);
bool cmd_expire(Repository& repo, std::uint64_t id);
bool cmd_delete(Repository& repo, std::uint64_t id, bool force);
bool cmd_list(const Repository& repo, std::string_view filter);
bool cmd_stats(const Repository& repo);

void print_help();
void print_banner();

}  // namespace tt::cli
