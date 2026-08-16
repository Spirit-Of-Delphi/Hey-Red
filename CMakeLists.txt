cmake_minimum_required(VERSION 3.10)
project(HeyRed VERSION 1.0.0 LANGUAGES CXX)

# Require C++17 standard for std::variant, std::shared_mutex, etc.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimize for performance (equivalent to -O3)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")

# Windows requires the ws2_32 library for sockets
if(WIN32)
    set(NETWORK_LIBS ws2_32)
else()
    set(NETWORK_LIBS pthread)
endif()

# 1. Build the Server
add_executable(heyred-server server.cpp)
target_link_libraries(heyred-server PRIVATE ${NETWORK_LIBS})

# 2. Build the Command Line Interface (CLI)
add_executable(heyred-cli miniredis-cli.cpp)
target_link_libraries(heyred-cli PRIVATE ${NETWORK_LIBS})

# 3. Build the Benchmark Suite
add_executable(heyred-benchmark benchmark.cpp)
target_link_libraries(heyred-benchmark PRIVATE ${NETWORK_LIBS})
