cmake_minimum_required(VERSION 3.20)
project(trade_tracker_cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(trade-tracker
    src/main.cpp
    src/cli/cli.cpp
    src/model/trade.cpp
    src/model/json.cpp
    src/storage/repository.cpp
)

target_include_directories(trade-tracker PRIVATE src)

if(MINGW)
    target_link_libraries(trade-tracker PRIVATE stdc++exp)
    target_link_options(trade-tracker PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(trade-tracker PRIVATE -Wall -Wextra -Wpedantic)
endif()
