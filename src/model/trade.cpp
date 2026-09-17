#include "model/trade.hpp"

#include <cmath>

namespace tt {

std::string_view to_string(TradeType t) noexcept {
    switch (t) {
        case TradeType::Long:
            return "LONG";
        case TradeType::Short:
            return "SHORT";
    }
    return "UNKNOWN";
}

std::string_view to_string(TradeStatus s) noexcept {
    switch (s) {
        case TradeStatus::Open:
            return "OPEN";
        case TradeStatus::Closed:
            return "CLOSED";
        case TradeStatus::Cancelled:
            return "CANCELLED";
    }
    return "UNKNOWN";
}

std::string_view to_string(Outcome o) noexcept {
    switch (o) {
        case Outcome::Win:
            return "WIN";
        case Outcome::Loss:
            return "LOSS";
        case Outcome::Breakeven:
            return "BREAKEVEN";
        case Outcome::Undefined:
            return "—";
    }
    return "—";
}

double Trade::risk_per_unit() const noexcept {
    return std::abs(entry_price - stop_loss);
}

double Trade::reward_per_unit() const noexcept {
    return std::abs(take_profit - entry_price);
}

double Trade::planned_rr_ratio() const noexcept {
    const double risk = risk_per_unit();
    if (risk == 0.0 || !std::isfinite(risk)) {
        return 0.0;
    }
    return reward_per_unit() / risk;
}

std::optional<double> Trade::realized_pnl_per_unit() const noexcept {
    if (status != TradeStatus::Closed || !exit_price.has_value()) {
        return std::nullopt;
    }
    const double exit = *exit_price;
    return (type == TradeType::Long) ? (exit - entry_price)
                                     : (entry_price - exit);
}

std::optional<double> Trade::realized_r_multiple() const noexcept {
    const double risk = risk_per_unit();
    if (risk == 0.0 || !std::isfinite(risk)) {
        return std::nullopt;
    }
    auto pnl = realized_pnl_per_unit();
    if (!pnl.has_value()) {
        return std::nullopt;
    }
    return *pnl / risk;
}

Outcome Trade::outcome() const noexcept {
    auto r = realized_r_multiple();
    if (!r.has_value()) {
        return Outcome::Undefined;
    }
    constexpr double epsilon = 1e-9;
    if (*r > epsilon) {
        return Outcome::Win;
    }
    if (*r < -epsilon) {
        return Outcome::Loss;
    }
    return Outcome::Breakeven;
}

std::optional<std::string> validate_prices(TradeType type,
                                           double entry,
                                           double stop,
                                           double target) noexcept {
    if (!std::isfinite(entry) || !std::isfinite(stop) || !std::isfinite(target)) {
        return "all prices must be finite numbers";
    }
    if (entry <= 0.0 || stop <= 0.0 || target <= 0.0) {
        return "all prices must be positive numbers greater than zero";
    }
    if (stop == entry) {
        return "stop loss cannot equal the entry price";
    }
    if (target == entry) {
        return "take profit cannot equal the entry price";
    }
    if (type == TradeType::Long) {
        if (!(stop < entry)) {
            return "for a LONG trade, stop loss must be below the entry price";
        }
        if (!(target > entry)) {
            return "for a LONG trade, take profit must be above the entry price";
        }
    } else {
        if (!(stop > entry)) {
            return "for a SHORT trade, stop loss must be above the entry price";
        }
        if (!(target < entry)) {
            return "for a SHORT trade, take profit must be below the entry price";
        }
    }
    return std::nullopt;
}

}  // namespace tt
