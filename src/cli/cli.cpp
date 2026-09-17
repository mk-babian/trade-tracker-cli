#include "cli/cli.hpp"

#include "model/trade.hpp"
#include "util/ansi.hpp"
#include "util/time_utils.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace tt::cli {

namespace {

std::filesystem::path default_data_file() {
    if (const char* env = std::getenv("TT_DATA_FILE"); env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    return std::filesystem::path("trades.json");
}

std::string trim(std::string s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    auto b = s.begin();
    auto e = s.end();
    while (b != e && is_ws(*b)) {
        ++b;
    }
    while (b != e && is_ws(*(e - 1))) {
        --e;
    }
    return std::string(b, e);
}

std::optional<std::string> read_line() {
    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;  // EOF
    }
    return line;
}

std::optional<std::string> prompt(const std::string& label) {
    std::print("{}{}{} ", ansi::cyan, label, ansi::reset);
    std::cout.flush();
    auto line = read_line();
    if (!line.has_value()) {
        return std::nullopt;
    }
    return trim(*line);
}

std::optional<double> parse_double(std::string_view s) {
    double v = 0.0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

std::optional<std::uint64_t> parse_id(std::string_view s) {
    if (s.empty()) {
        return std::nullopt;
    }
    std::uint64_t v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

std::optional<double> prompt_double(const std::string& label) {
    while (true) {
        auto raw = prompt(label);
        if (!raw.has_value() || raw->empty()) {
            return std::nullopt;  // EOF or blank -> abort
        }
        auto v = parse_double(*raw);
        if (v.has_value()) {
            return v;
        }
        std::println(stderr, "{}Invalid number: '{}'{}", ansi::bright_red, *raw,
                     ansi::reset);
    }
}

bool prompt_yes_no(const std::string& label) {
    while (true) {
        auto raw = prompt(label + " [y/N]");
        if (!raw.has_value() || raw->empty()) {
            return false;
        }
        char c = static_cast<char>(std::tolower(static_cast<unsigned char>((*raw)[0])));
        if (c == 'y') {
            return true;
        }
        if (c == 'n') {
            return false;
        }
        std::println(stderr, "{}Please answer y or n.{}", ansi::bright_yellow, ansi::reset);
    }
}

std::string format_price(double v) {
    return std::format("{:.2f}", v);
}

std::string format_optional_price(std::optional<double> v) {
    if (v.has_value()) {
        return format_price(*v);
    }
    return "—";
}

}  // namespace

namespace {

// Options for flag-driven (non-interactive) `add`.
struct AddOptions {
    std::optional<TradeType> type;
    std::optional<double> entry;
    std::optional<double> stop;
    std::optional<double> tp;
    std::optional<std::string> notes;
    bool yes = false;
    std::optional<std::string> error;
};

AddOptions parse_add_flags(const std::vector<std::string_view>& args) {
    AddOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        auto next = [&]() -> std::optional<std::string_view> {
            if (i + 1 < args.size()) {
                return args[i + 1];
            }
            return std::nullopt;
        };
        if (a == "--type") {
            auto v = next();
            if (!v.has_value()) {
                o.error = "--type requires a value (long|short)";
                return o;
            }
            if (*v == "long") {
                o.type = TradeType::Long;
            } else if (*v == "short") {
                o.type = TradeType::Short;
            } else {
                o.error = "invalid --type value (expected long|short)";
                return o;
            }
            ++i;
        } else if (a == "--entry") {
            auto v = next();
            if (!v.has_value() || !parse_double(*v).has_value()) {
                o.error = "--entry requires a numeric value";
                return o;
            }
            o.entry = parse_double(*v);
            ++i;
        } else if (a == "--stop") {
            auto v = next();
            if (!v.has_value() || !parse_double(*v).has_value()) {
                o.error = "--stop requires a numeric value";
                return o;
            }
            o.stop = parse_double(*v);
            ++i;
        } else if (a == "--tp" || a == "--take-profit") {
            auto v = next();
            if (!v.has_value() || !parse_double(*v).has_value()) {
                o.error = "--tp requires a numeric value";
                return o;
            }
            o.tp = parse_double(*v);
            ++i;
        } else if (a == "--notes" || a == "--desc") {
            auto v = next();
            if (!v.has_value()) {
                o.error = "--notes requires a string value";
                return o;
            }
            o.notes = std::string(*v);
            ++i;
        } else if (a == "--yes" || a == "-y") {
            o.yes = true;
        } else {
            o.error = "unknown flag: " + std::string(a);
            return o;
        }
    }
    return o;
}

}  // namespace


bool cmd_add(Repository& repo, const std::vector<std::string_view>& args) {
    // IMPORTANT: capture the draft-start timestamp as the very first action,
    // before any flags are parsed or any user input is solicited. This makes
    // `creation_draft_start` reflect the moment the `add` workflow began.
    const auto creation_draft_start = time_utils::now();

    const auto flags = parse_add_flags(args);
    if (flags.error.has_value()) {
        std::println(stderr, "{}error: {}{}", ansi::bright_red, *flags.error, ansi::reset);
        return false;
    }

    // Determine whether we can run fully non-interactively.
    const bool interactive = !flags.type.has_value() || !flags.entry.has_value() ||
                             !flags.stop.has_value() || !flags.tp.has_value();

    if (interactive) {
        std::println("{}— New Trade —{}", ansi::bold, ansi::reset);
    }

    // Direction.
    TradeType type;
    if (flags.type.has_value()) {
        type = *flags.type;
    } else {
        while (true) {
            auto v = prompt("Direction [L]ong / [S]hort:");
            if (!v.has_value()) {
                std::println("Aborted.");
                return false;
            }
            if (v->empty()) {
                continue;
            }
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>((*v)[0])));
            if (c == 'l') {
                type = TradeType::Long;
                break;
            }
            if (c == 's') {
                type = TradeType::Short;
                break;
            }
            std::println(stderr, "{}Please enter L or S.{}", ansi::bright_yellow, ansi::reset);
        }
    }

    // Entry price.
    double entry;
    if (flags.entry.has_value()) {
        entry = *flags.entry;
    } else {
        auto v = prompt_double("Entry price:");
        if (!v.has_value()) {
            std::println("Aborted.");
            return false;
        }
        entry = *v;
    }

    // Stop loss.
    double stop;
    if (flags.stop.has_value()) {
        stop = *flags.stop;
    } else {
        auto v = prompt_double("Stop loss:");
        if (!v.has_value()) {
            std::println("Aborted.");
            return false;
        }
        stop = *v;
    }

    // Take profit.
    double tp;
    if (flags.tp.has_value()) {
        tp = *flags.tp;
    } else {
        auto v = prompt_double("Take profit:");
        if (!v.has_value()) {
            std::println("Aborted.");
            return false;
        }
        tp = *v;
    }

    // Description / notes.
    std::string description;
    if (flags.notes.has_value()) {
        description = *flags.notes;
    } else if (interactive) {
        auto v = prompt("Description (optional):");
        if (v.has_value()) {
            description = *v;
        }
    }

    // Validate the setup.
    if (auto err = validate_prices(type, entry, stop, tp); err.has_value()) {
        std::println(stderr, "{}Validation error: {}{}", ansi::bright_red, *err, ansi::reset);
        return false;
    }


    // Build the trade (status OPEN, exit fields empty).
    Trade trade;
    trade.id = repo.next_id();
    trade.type = type;
    trade.status = TradeStatus::Open;
    trade.entry_price = entry;
    trade.stop_loss = stop;
    trade.take_profit = tp;
    trade.description = description;
    trade.creation_draft_start = creation_draft_start;

    // Instant calculation summary (planned R:R).
    const double risk = trade.risk_per_unit();
    const double reward = trade.reward_per_unit();
    const double rr = trade.planned_rr_ratio();

    std::println("");
    std::println("{}── Trade Summary ──{}", ansi::bold, ansi::reset);
    std::println("  Direction : {}", (type == TradeType::Long) ? "LONG" : "SHORT");
    std::println("  Entry     : {}", format_price(entry));
    std::println("  Stop loss : {}  (risk {})", format_price(stop), format_price(risk));
    std::println("  Take prof : {}  (reward {})", format_price(tp), format_price(reward));
    std::println("  {}Planned R:R : {:.2f}{}", ansi::bold, rr, ansi::reset);
    if (!description.empty()) {
        std::println("  Notes     : {}", description);
    }

    // Final confirmation.
    const bool confirmed = flags.yes || prompt_yes_no("Save this trade?");
    if (!confirmed) {
        std::println("{}Trade discarded.{}", ansi::yellow, ansi::reset);
        return false;
    }

    // Confirmation timestamp = the moment the entry was committed.
    trade.entry_time = time_utils::now();
    repo.add(std::move(trade));

    std::string err;
    if (!repo.save(err)) {
        std::println(stderr, "{}error: failed to save: {}{}", ansi::bright_red, err, ansi::reset);
        return false;
    }

    std::println("{}Trade #{} saved as OPEN.{}", ansi::green, repo.all().back().id, ansi::reset);
    return true;
}


bool cmd_close(Repository& repo, std::uint64_t id) {
    Trade* t = repo.find(id);
    if (t == nullptr) {
        std::println(stderr, "{}No trade with id {}.{}", ansi::bright_red, id, ansi::reset);
        return false;
    }
    if (t->status != TradeStatus::Open) {
        std::println(stderr, "{}Trade {} is not OPEN (status: {}).{}", ansi::bright_red, id,
                     tt::to_string(t->status), ansi::reset);
        return false;
    }

    std::println("{}Closing trade #{} ({} @ {}){}", ansi::bold, id, tt::to_string(t->type),
                 format_price(t->entry_price), ansi::reset);

    auto exit = prompt_double("Exit price:");
    if (!exit.has_value()) {
        std::println("Aborted.");
        return false;
    }
    if (!std::isfinite(*exit) || *exit <= 0.0) {
        std::println(stderr, "{}Exit price must be a positive finite number.{}", ansi::bright_red,
                     ansi::reset);
        return false;
    }

    std::string notes;
    auto nv = prompt("Exit notes (optional):");
    if (nv.has_value()) {
        notes = *nv;
    }

    Trade updated = *t;
    updated.exit_price = *exit;
    updated.exit_notes = notes;
    updated.exit_time = time_utils::now();
    updated.status = TradeStatus::Closed;

    // Final result summary.
    const auto pnl = updated.realized_pnl_per_unit();
    const auto r_multiple = updated.realized_r_multiple();
    const Outcome outcome = updated.outcome();

    std::string_view color = ansi::bright_yellow;
    if (outcome == Outcome::Win) {
        color = ansi::bright_green;
    } else if (outcome == Outcome::Loss) {
        color = ansi::bright_red;
    }

    std::println("");
    std::println("{}── Close Result ──{}", ansi::bold, ansi::reset);
    std::println("  Exit price : {}", format_price(*exit));
    std::println("  PnL/unit   : {}{:+.2f}{}", color, pnl.value_or(0.0), ansi::reset);
    std::println("  R-multiple : {}{:+.2f} R{}", color, r_multiple.value_or(0.0), ansi::reset);
    std::println("  Outcome    : {}{}{}", color, tt::to_string(outcome), ansi::reset);

    repo.update(updated);

    std::string err;
    if (!repo.save(err)) {
        std::println(stderr, "{}error: failed to save: {}{}", ansi::bright_red, err, ansi::reset);
        return false;
    }

    std::println("{}Trade #{} closed.{}", ansi::green, id, ansi::reset);
    return true;
}

bool cmd_list(const Repository& repo, std::string_view filter) {
    const auto& trades = repo.all();
    if (trades.empty()) {
        std::println("No trades recorded yet. Use {}add{} to create one.", ansi::cyan, ansi::reset);
        return true;
    }

    auto matches = [&](const Trade& t) {
        if (filter == "all") {
            return true;
        }
        if (filter == "closed") {
            return t.status == TradeStatus::Closed;
        }
        // default: open
        return t.status == TradeStatus::Open;
    };

    std::size_t shown = 0;
    for (const auto& t : trades) {
        if (matches(t)) {
            ++shown;
        }
    }

    if (shown == 0) {
        std::println("No {} trades.", filter);
        return true;
    }

    // Header.
    std::println("");
    std::println("{:<4} {:<6} {:<9} {:>10} {:>10} {:>10} {:>10} {:>8} {:>8} {:<9} {}",
                 "ID", "TYPE", "STATUS", "ENTRY", "STOP", "TARGET", "EXIT", "R:R",
                 "R-MULT", "OUTCOME", "ENTRY TIME");
    std::println("{}", std::string(100, '-'));

    for (const auto& t : trades) {
        if (!matches(t)) {
            continue;
        }
        std::string_view type_color = (t.type == TradeType::Long) ? ansi::bright_green
                                                                  : ansi::bright_red;
        std::string_view outcome_color = ansi::white;
        if (t.status == TradeStatus::Closed) {
            switch (t.outcome()) {
                case Outcome::Win:
                    outcome_color = ansi::bright_green;
                    break;
                case Outcome::Loss:
                    outcome_color = ansi::bright_red;
                    break;
                case Outcome::Breakeven:
                    outcome_color = ansi::bright_yellow;
                    break;
                case Outcome::Undefined:
                    outcome_color = ansi::white;
                    break;
            }
        }

        const std::string exit_str = format_optional_price(t.exit_price);
        const std::string r_mult_str = t.realized_r_multiple().has_value()
                                           ? std::format("{:+.2f}", *t.realized_r_multiple())
                                           : "—";

        std::println(
            "{:<4} {}{:<6}{} {:<9} {:>10} {:>10} {:>10} {:>10} {:>8.2f} {}{:>8}{} {}{:<9}{} {}",
            t.id, type_color, tt::to_string(t.type), ansi::reset, tt::to_string(t.status),
            format_price(t.entry_price), format_price(t.stop_loss), format_price(t.take_profit),
            exit_str, t.planned_rr_ratio(), outcome_color, r_mult_str, ansi::reset,
            outcome_color, (t.status == TradeStatus::Closed) ? tt::to_string(t.outcome()) : "—",
            ansi::reset, time_utils::format(t.entry_time));
    }
    std::println("");
    std::println("{} trade(s) shown.", shown);
    return true;
}


bool cmd_stats(const Repository& repo) {
    const auto& trades = repo.all();

    std::size_t total = trades.size();
    std::size_t open = 0;
    std::size_t closed = 0;
    std::size_t cancelled = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t breakeven = 0;
    double total_r = 0.0;
    std::size_t closed_count = 0;
    double planned_sum = 0.0;
    std::size_t planned_count = 0;

    for (const auto& t : trades) {
        switch (t.status) {
            case TradeStatus::Open:
                ++open;
                break;
            case TradeStatus::Closed:
                ++closed;
                break;
            case TradeStatus::Cancelled:
                ++cancelled;
                break;
        }

        if (t.status == TradeStatus::Closed) {
            auto r = t.realized_r_multiple();
            if (r.has_value()) {
                total_r += *r;
                ++closed_count;
            }
            switch (t.outcome()) {
                case Outcome::Win:
                    ++wins;
                    break;
                case Outcome::Loss:
                    ++losses;
                    break;
                case Outcome::Breakeven:
                    ++breakeven;
                    break;
                case Outcome::Undefined:
                    break;
            }
        }

        const double rr = t.planned_rr_ratio();
        if (std::isfinite(rr) && rr > 0.0) {
            planned_sum += rr;
            ++planned_count;
        }
    }

    const double win_rate = closed_count > 0 ? 100.0 * static_cast<double>(wins) /
                                                   static_cast<double>(closed_count)
                                             : 0.0;
    const double avg_r = closed_count > 0 ? total_r / static_cast<double>(closed_count) : 0.0;
    const double avg_planned =
        planned_count > 0 ? planned_sum / static_cast<double>(planned_count) : 0.0;

    std::println("");
    std::println("{}── Statistics ──{}", ansi::bold, ansi::reset);
    std::println("  Total trades    : {}", total);
    std::println("  Open            : {}", open);
    std::println("  Closed          : {}", closed);
    std::println("  Cancelled       : {}", cancelled);
    std::println("  Wins            : {}{}{}", ansi::bright_green, wins, ansi::reset);
    std::println("  Losses          : {}{}{}", ansi::bright_red, losses, ansi::reset);
    std::println("  Breakeven       : {}", breakeven);
    std::println("  {}Win rate       : {:.2f}%{}", ansi::bold, win_rate, ansi::reset);
    std::println("  Total R-multiple: {:+.2f} R", total_r);
    std::println("  Avg R-multiple  : {:+.2f} R", avg_r);
    std::println("  Avg planned R:R : {:.2f}", avg_planned);
    std::println("");
    return true;
}


void print_banner() {
    std::println("{}Trade Tracker CLI{}", ansi::bold, ansi::reset);
    std::println("{}Day trading journal & R-multiple tracker{}", ansi::dim, ansi::reset);
}

void print_help() {
    print_banner();
    std::println("");
    std::println("{}Usage:{} trade-tracker <command> [options]", ansi::bold, ansi::reset);
    std::println("");
    std::println("{}Commands:{}", ansi::bold, ansi::reset);
    std::println("  {}add{}                     Add a new trade (interactive)", ansi::cyan, ansi::reset);
    std::println("  {}add --type long --entry 100 --stop 95 --tp 110 [--notes \"...\"] [--yes]{}",
                 ansi::cyan, ansi::reset);
    std::println("                            Non-interactive add (see flags below)");
    std::println("  {}close <id>{}              Close an OPEN trade by id", ansi::cyan, ansi::reset);
    std::println("  {}list [open|closed|all]{}  List trades (default: open)", ansi::cyan, ansi::reset);
    std::println("  {}stats{}                   Show aggregate statistics", ansi::cyan, ansi::reset);
    std::println("  {}help{}                    Show this help", ansi::cyan, ansi::reset);
    std::println("");
    std::println("{}Add flags:{}", ansi::bold, ansi::reset);
    std::println("  --type long|short    Direction");
    std::println("  --entry <number>     Entry price");
    std::println("  --stop <number>      Stop loss price");
    std::println("  --tp <number>        Take profit price");
    std::println("  --notes <text>       Optional description");
    std::println("  --yes / -y           Skip confirmation prompt");
    std::println("");
    std::println("{}Global option:{}", ansi::bold, ansi::reset);
    std::println("  --file <path> / -f   Journal file (default: ./trades.json, or $TT_DATA_FILE)");
}

int run(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_help();
        return 1;
    }

    std::filesystem::path data_file = default_data_file();
    std::vector<std::string_view> rest;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        if (a == "--file" || a == "-f") {
            if (i + 1 >= args.size()) {
                std::println(stderr, "{}error: {} requires a path{}", ansi::bright_red, a,
                             ansi::reset);
                return 2;
            }
            data_file = std::filesystem::path(args[++i]);
        } else {
            rest.push_back(a);
        }
    }

    if (rest.empty()) {
        print_help();
        return 1;
    }

    const std::string_view cmd = rest[0];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_help();
        return 0;
    }

    Repository repo(data_file);
    std::string err;
    if (!repo.load(err)) {
        std::println(stderr, "{}error: failed to load journal: {}{}", ansi::bright_red, err,
                     ansi::reset);
        return 3;
    }

    if (cmd == "add") {
        std::vector<std::string_view> add_args(rest.begin() + 1, rest.end());
        return cmd_add(repo, add_args) ? 0 : 1;
    }
    if (cmd == "close") {
        if (rest.size() < 2) {
            std::println(stderr, "{}usage: close <id>{}", ansi::bright_red, ansi::reset);
            return 2;
        }
        auto id = parse_id(rest[1]);
        if (!id.has_value()) {
            std::println(stderr, "{}invalid id: '{}'{}", ansi::bright_red, rest[1], ansi::reset);
            return 2;
        }
        return cmd_close(repo, *id) ? 0 : 1;
    }
    if (cmd == "list") {
        std::string_view filter = (rest.size() > 1) ? rest[1] : std::string_view{"open"};
        if (filter != "open" && filter != "closed" && filter != "all") {
            std::println(stderr, "{}invalid filter: '{}' (expected open|closed|all){}",
                         ansi::bright_red, filter, ansi::reset);
            return 2;
        }
        return cmd_list(repo, filter) ? 0 : 1;
    }
    if (cmd == "stats") {
        return cmd_stats(repo) ? 0 : 1;
    }

    std::println(stderr, "{}unknown command: '{}'{}", ansi::bright_red, cmd, ansi::reset);
    std::println(stderr, "Run {}help{} for usage.", ansi::cyan, ansi::reset);
    return 2;
}

}  // namespace tt::cli

