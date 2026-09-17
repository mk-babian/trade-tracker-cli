#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tt {

enum class TradeType { Long, Short };
enum class TradeStatus { Open, Closed, Cancelled };
enum class Outcome { Win, Loss, Breakeven, Undefined };

std::string_view to_string(TradeType t) noexcept;
std::string_view to_string(TradeStatus s) noexcept;
std::string_view to_string(Outcome o) noexcept;

// Pure data model for a single trade. Contains no I/O or persistence logic;
// all derived/calculated values are computed on demand from the raw fields.
struct Trade {
    std::uint64_t id{0};
    TradeType type{TradeType::Long};
    TradeStatus status{TradeStatus::Open};

    double entry_price{0.0};
    double stop_loss{0.0};
    double take_profit{0.0};
    std::optional<double> exit_price{};

    std::string description{};
    std::string exit_notes{};

    // Timestamps. See time_utils.hpp for the on-disk representation.
    std::chrono::system_clock::time_point creation_draft_start{};
    std::chrono::system_clock::time_point entry_time{};
    std::optional<std::chrono::system_clock::time_point> exit_time{};

    // ---- Calculated (automated) ----

    // Risk per unit: |entry - stop_loss|.
    double risk_per_unit() const noexcept;

    // Reward per unit: |take_profit - entry|.
    double reward_per_unit() const noexcept;

    // Planned reward:risk ratio (reward / risk). 0.0 if risk is zero/invalid.
    double planned_rr_ratio() const noexcept;

    // Signed profit/loss per unit, only meaningful when CLOSED with an exit.
    // Long:  exit - entry;   Short: entry - exit.
    std::optional<double> realized_pnl_per_unit() const noexcept;

    // Realized R-multiple = pnl_per_unit / risk_per_unit. Only when closed.
    std::optional<double> realized_r_multiple() const noexcept;

    Outcome outcome() const noexcept;
};

// Validate that entry/stop/target are mutually consistent with the trade
// direction. Returns a human-readable error message on failure, or nullopt
// when the prices form a valid trade setup.
std::optional<std::string> validate_prices(TradeType type,
                                           double entry,
                                           double stop,
                                           double target) noexcept;

}  // namespace tt
