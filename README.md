### [WARNING]
I vibe coded this entirely.

# Trade Tracker CLI

A modular, single-binary C++23 command-line day-trading journal that tracks
trades, planned/realized R:R ratios, and aggregate performance statistics.

## Requirements

- A C++23 compiler with `<print>` support:
  - **GCC 13.2+** (recommended 14+) — `<print>`/`std::print` are available.
  - **Clang 16+** with a matching libstdc++ (Clang's own libc++ `<print>`
    support lags, so building against libstdc++ is recommended).
- CMake 3.20+ (optional — a plain compiler invocation also works).

## Build

### With CMake

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/trade-tracker help
```

### Direct compiler invocation

```sh
g++ -std=c++23 -O2 -Wall -Wextra -Wpedantic \
    -Isrc \
    src/main.cpp src/cli/cli.cpp src/model/trade.cpp \
    src/model/json.cpp src/storage/repository.cpp \
    -o trade-tracker
```

(Clang: `clang++ -std=c++23 ... -stdlib=libstdc++`.)

## Usage

```sh
# Interactive add
./trade-tracker add

# Flag-driven (non-interactive) add
./trade-tracker add --type long --entry 100 --stop 95 --tp 110 --notes "breakout" --yes

# Close an open trade
./trade-tracker close 1

# Mark an open trade as expired (entry price never hit within the timeframe)
./trade-tracker expire 1

# Delete a trade (prompts for confirmation)
./trade-tracker delete 1
./trade-tracker delete 1 --yes   # skip confirmation

# List
./trade-tracker list            # open trades (default)
./trade-tracker list all
./trade-tracker list closed
./trade-tracker list expired

# Aggregate stats
./trade-tracker stats
```

Journal data is persisted to `./trades.json` by default. Override with the
`--file <path>` global flag or the `TT_DATA_FILE` environment variable.

## Architecture

```
src/
├── main.cpp                  # process entry point
├── model/                    # domain layer
│   ├── trade.hpp / .cpp      # Trade entity + validation + calculated fields
│   └── json.hpp / .cpp       # minimal JSON DOM / parser / serializer
├── storage/
│   └── repository.hpp / .cpp # JSON-file persistence (load/save/CRUD)
├── cli/
│   └── cli.hpp / .cpp        # command parsing, interactive prompts, formatting
└── util/
    ├── ansi.hpp              # ANSI SGR color codes
    └── time_utils.hpp        # <chrono> timestamp <-> int64 micros + display
```

The layering is strict: `cli` depends on `storage` and `model`, `storage`
depends on `model` (plus `json`), and `model` has no dependencies beyond the
standard library. No external libraries are used.

## How draft timing is captured accurately

`creation_draft_start` is taken as the **first statement** inside `cmd_add`,
before any flags are parsed and before any user input is solicited:

```cpp
bool cmd_add(Repository& repo, const std::vector<std::string_view>& args) {
    const auto creation_draft_start = time_utils::now();  // <-- captured here
    ...
}
```

This guarantees the timestamp reflects the instant the `add` workflow began,
not the moment the user finished typing. A separate `entry_time` is captured
only when the user confirms and the trade is actually persisted, so the
"thinking time" between draft start and commit is measured and stored
independently. Both timestamps use `std::chrono::system_clock` (nanosecond
resolution on Linux) and are persisted as microsecond counts since the Unix
epoch, so no sub-second precision is lost on a round-trip.
