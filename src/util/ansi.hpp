#pragma once

#include <string_view>

// Minimal ANSI SGR (Select Graphic Rendition) escape sequences for terminal
// styling. Kept in a header-only namespace so any layer can use it without
// pulling in heavier dependencies.
namespace tt::ansi {

inline constexpr std::string_view reset = "\033[0m";
inline constexpr std::string_view bold = "\033[1m";
inline constexpr std::string_view dim = "\033[2m";
inline constexpr std::string_view underline = "\033[4m";

inline constexpr std::string_view black = "\033[30m";
inline constexpr std::string_view red = "\033[31m";
inline constexpr std::string_view green = "\033[32m";
inline constexpr std::string_view yellow = "\033[33m";
inline constexpr std::string_view blue = "\033[34m";
inline constexpr std::string_view magenta = "\033[35m";
inline constexpr std::string_view cyan = "\033[36m";
inline constexpr std::string_view white = "\033[37m";

inline constexpr std::string_view bright_black = "\033[90m";
inline constexpr std::string_view bright_red = "\033[91m";
inline constexpr std::string_view bright_green = "\033[92m";
inline constexpr std::string_view bright_yellow = "\033[93m";
inline constexpr std::string_view bright_blue = "\033[94m";
inline constexpr std::string_view bright_magenta = "\033[95m";
inline constexpr std::string_view bright_cyan = "\033[96m";
inline constexpr std::string_view bright_white = "\033[97m";

}  // namespace tt::ansi
