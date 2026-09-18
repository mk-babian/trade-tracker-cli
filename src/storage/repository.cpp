#include "storage/repository.hpp"

#include "model/json.hpp"
#include "util/time_utils.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace tt {

namespace {

json::Object trade_to_json(const Trade& t) {
    json::Object o;
    o["id"] = json::Value{static_cast<std::int64_t>(t.id)};
    o["type"] = json::Value{std::string(to_string(t.type))};
    o["status"] = json::Value{std::string(to_string(t.status))};
    o["entry_price"] = json::Value{t.entry_price};
    o["stop_loss"] = json::Value{t.stop_loss};
    o["take_profit"] = json::Value{t.take_profit};
    if (t.exit_price.has_value()) {
        o["exit_price"] = json::Value{*t.exit_price};
    } else {
        o["exit_price"] = json::Value{};
    }
    o["description"] = json::Value{t.description};
    o["exit_notes"] = json::Value{t.exit_notes};
    o["creation_draft_start"] = json::Value{time_utils::to_micros(t.creation_draft_start)};
    o["entry_time"] = json::Value{time_utils::to_micros(t.entry_time)};
    if (t.exit_time.has_value()) {
        o["exit_time"] = json::Value{time_utils::to_micros(*t.exit_time)};
    } else {
        o["exit_time"] = json::Value{};
    }
    return o;
}

bool parse_type(std::string_view s, TradeType& out) {
    if (s == "LONG") {
        out = TradeType::Long;
        return true;
    }
    if (s == "SHORT") {
        out = TradeType::Short;
        return true;
    }
    return false;
}

bool parse_status(std::string_view s, TradeStatus& out) {
    if (s == "OPEN") {
        out = TradeStatus::Open;
        return true;
    }
    if (s == "CLOSED") {
        out = TradeStatus::Closed;
        return true;
    }
    if (s == "CANCELLED") {
        out = TradeStatus::Cancelled;
        return true;
    }
    if (s == "EXPIRED") {
        out = TradeStatus::Expired;
        return true;
    }
    return false;
}

std::optional<Trade> trade_from_json(const json::Value& v) {
    if (!v.is_object()) {
        return std::nullopt;
    }
    const auto& o = v.as_object();
    auto has = [&](std::string_view k) { return o.find(k) != o.end(); };
    if (!has("id") || !has("type") || !has("status") || !has("entry_price") ||
        !has("stop_loss") || !has("take_profit")) {
        return std::nullopt;
    }
    try {
        Trade t;
        t.id = static_cast<std::uint64_t>(v.at("id").as_int());
        if (!parse_type(v.at("type").as_string(), t.type)) {
            return std::nullopt;
        }
        if (!parse_status(v.at("status").as_string(), t.status)) {
            return std::nullopt;
        }
        t.entry_price = v.at("entry_price").as_double();
        t.stop_loss = v.at("stop_loss").as_double();
        t.take_profit = v.at("take_profit").as_double();
        if (v.contains("exit_price") && v.at("exit_price").is_number()) {
            t.exit_price = v.at("exit_price").as_double();
        }
        if (v.contains("description") && v.at("description").is_string()) {
            t.description = v.at("description").as_string();
        }
        if (v.contains("exit_notes") && v.at("exit_notes").is_string()) {
            t.exit_notes = v.at("exit_notes").as_string();
        }
        if (v.contains("creation_draft_start")) {
            t.creation_draft_start =
                time_utils::from_micros(v.at("creation_draft_start").as_int());
        }
        if (v.contains("entry_time")) {
            t.entry_time = time_utils::from_micros(v.at("entry_time").as_int());
        }
        if (v.contains("exit_time") && v.at("exit_time").is_number()) {
            t.exit_time = time_utils::from_micros(v.at("exit_time").as_int());
        }
        return t;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

Repository::Repository(std::filesystem::path path) : path_(std::move(path)) {}

bool Repository::load(std::string& error) {
    trades_.clear();
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
        return true;  // fresh journal
    }
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        error = "cannot open " + path_.string();
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    if (text.empty()) {
        return true;
    }

    std::string parse_err;
    json::Value root = json::parse(text, parse_err);
    if (!parse_err.empty()) {
        error = parse_err;
        return false;
    }
    if (!root.is_array()) {
        error = "expected a top-level JSON array in " + path_.string();
        return false;
    }
    for (const auto& item : root.as_array()) {
        auto t = trade_from_json(item);
        if (!t.has_value()) {
            error = "malformed trade record in " + path_.string();
            return false;
        }
        trades_.push_back(std::move(*t));
    }
    return true;
}

bool Repository::save(std::string& error) const {
    json::Array arr;
    arr.reserve(trades_.size());
    for (const auto& t : trades_) {
        arr.push_back(json::Value{trade_to_json(t)});
    }
    const std::string text = json::dump_pretty(json::Value{std::move(arr)});

    // Atomic write: serialize to a temp file, then rename over the target so a
    // crash mid-write never leaves a truncated journal.
    auto tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + tmp.string();
            return false;
        }
        out << text;
        if (!out) {
            error = "write failed for " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        // Fall back for filesystems without atomic rename semantics.
        std::filesystem::copy_file(tmp, path_,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "cannot replace " + path_.string();
            return false;
        }
        std::filesystem::remove(tmp, ec);
    }
    return true;
}

std::uint64_t Repository::next_id() const noexcept {
    std::uint64_t max_id = 0;
    for (const auto& t : trades_) {
        if (t.id > max_id) {
            max_id = t.id;
        }
    }
    return max_id + 1;
}

const Trade* Repository::find(std::uint64_t id) const noexcept {
    for (const auto& t : trades_) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

Trade* Repository::find(std::uint64_t id) noexcept {
    for (auto& t : trades_) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

void Repository::add(Trade trade) {
    trades_.push_back(std::move(trade));
}

bool Repository::update(const Trade& trade) noexcept {
    for (auto& t : trades_) {
        if (t.id == trade.id) {
            t = trade;
            return true;
        }
    }
    return false;
}

bool Repository::remove(std::uint64_t id) noexcept {
    for (auto it = trades_.begin(); it != trades_.end(); ++it) {
        if (it->id == id) {
            trades_.erase(it);
            return true;
        }
    }
    return false;
}

}  // namespace tt

