#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

// Time helpers built exclusively on <chrono>.
//
// Persistence stores timestamps as signed 64-bit counts of microseconds since
// the Unix epoch. This is lossless for system_clock (which on Linux has
// nanosecond resolution) and trivially round-trippable, unlike text ISO-8601
// parsing which can lose sub-second precision or timezone information.
//
// Display formatting is done lazily in ::format() using the C++20 chrono
// calendar/zoned_time facilities, so the on-disk representation stays simple
// and precise while the user-visible output is human readable.
namespace tt::time_utils {

using clock = std::chrono::system_clock;
using time_point = clock::time_point;

inline time_point now() {
    return clock::now();
}

inline std::int64_t to_micros(time_point tp) {
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch())
        .count();
}

inline time_point from_micros(std::int64_t us) {
    return time_point{std::chrono::microseconds{us}};
}

// Format a time_point as a local-time string with microsecond precision,
// e.g. "2026-09-17 21:42:03.123456".
inline std::string format(time_point tp) {
    using namespace std::chrono;

    auto micros = time_point_cast<microseconds>(tp);
    auto secs = time_point_cast<seconds>(micros);
    auto frac = micros - secs;  // remainder: [0, 1) seconds, in microseconds

    std::string base;
    try {
        // Local wall-clock time via the system timezone database.
        base = std::format("{:%Y-%m-%d %H:%M:%S}", zoned_time{current_zone(), secs});
    } catch (...) {
        // Fall back to UTC if the tz database is unavailable.
        base = std::format("{:%Y-%m-%d %H:%M:%S}", secs);
    }
    return std::format("{}.{:06}", base, frac.count());
}

inline std::string format(std::optional<time_point> tp) {
    if (tp.has_value()) {
        return format(*tp);
    }
    return "—";
}

}  // namespace tt::time_utils
