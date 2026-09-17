#pragma once

#include "model/trade.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tt {

// Lightweight JSON-file-backed repository of trades. Owns the in-memory
// collection and provides load/save plus simple CRUD. It knows nothing about
// the CLI or terminal formatting; serialization of the Trade model lives here
// (in repository.cpp) as a persistence concern.
class Repository {
public:
    explicit Repository(std::filesystem::path path);

    // Load trades from disk. A missing file is treated as a fresh, empty
    // journal (returns true). Returns false and sets `error` on read/parse
    // failure.
    bool load(std::string& error);

    // Persist trades to disk atomically (temp file + rename).
    bool save(std::string& error) const;

    // Smallest unused id (max existing id + 1, or 1 for an empty journal).
    std::uint64_t next_id() const noexcept;

    const Trade* find(std::uint64_t id) const noexcept;
    Trade* find(std::uint64_t id) noexcept;

    const std::vector<Trade>& all() const noexcept { return trades_; }
    std::vector<Trade>& all() noexcept { return trades_; }

    void add(Trade trade);

    // Replace an existing trade by id; returns false if not found.
    bool update(const Trade& trade) noexcept;

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    std::vector<Trade> trades_;
};

}  // namespace tt
