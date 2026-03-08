cmake_minimum_required(VERSION 3.20)
project(hf_marketmaking LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -fsanitize=address,undefined")

# Fetch all dependencies
include(cmake/Dependencies.cmake)

# -----------------------------------------------------------------------
# hfmm_core — static library with all implementation files
# -----------------------------------------------------------------------
add_library(hfmm_core STATIC
    src/core/order_book.cpp
    src/strategy/avellaneda_stoikov.cpp
    src/execution/binance_rest.cpp
    src/execution/order_manager.cpp
    src/feed/market_data_feed.cpp
    src/feed/binance_feed.cpp
    src/feed/coinbase_feed.cpp
    src/engine/engine.cpp
)

target_include_directories(hfmm_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(hfmm_core PUBLIC
    ixwebsocket
    simdjson
    nlohmann_json::nlohmann_json
    CURL::libcurl
    OpenSSL::Crypto
    OpenSSL::SSL
)

target_compile_options(hfmm_core PRIVATE -Wall -Wextra -Wpedantic)

# -----------------------------------------------------------------------
# hfmm — main executable
# -----------------------------------------------------------------------
add_executable(hfmm app/main.cpp)
target_link_libraries(hfmm PRIVATE hfmm_core)
target_include_directories(hfmm PRIVATE ${PROJECT_SOURCE_DIR}/include)

# -----------------------------------------------------------------------
# coinbase_level2_recorder — example app to persist Coinbase L2 stream
# -----------------------------------------------------------------------
add_executable(coinbase_level2_recorder app/coinbase_level2_recorder.cpp)
target_link_libraries(coinbase_level2_recorder PRIVATE hfmm_core)
target_include_directories(coinbase_level2_recorder PRIVATE ${PROJECT_SOURCE_DIR}/include)

# -----------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
