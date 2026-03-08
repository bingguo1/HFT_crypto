include(FetchContent)

# -----------------------------------------------------------------------
# ixwebsocket v11.4.5
# -----------------------------------------------------------------------
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.5
    GIT_SHALLOW    TRUE
)
set(USE_TLS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

# -----------------------------------------------------------------------
# simdjson v3.10.1
# -----------------------------------------------------------------------
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.10.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(simdjson)

# -----------------------------------------------------------------------
# nlohmann/json v3.11.3
# -----------------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# -----------------------------------------------------------------------
# GoogleTest v1.14.0
# -----------------------------------------------------------------------
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(BUILD_GMOCK    OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# -----------------------------------------------------------------------
# System: libcurl + OpenSSL
# -----------------------------------------------------------------------
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
