cmake_minimum_required(VERSION 3.20)
project(PowerSlave_ENet_Prototype LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Statically link MSVC runtime (/MT) for standalone portable binaries
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Windows MSVC Settings
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

# Build Official ENet library from third_party
add_subdirectory(third_party/enet EXCLUDE_FROM_ALL)
target_include_directories(enet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/include)
target_link_libraries(enet PUBLIC ws2_32 winmm)

# Common Core Library
add_library(pse_core STATIC
    src/net_protocol.cpp
    src/simulation.cpp
    src/net_simulator.cpp
    src/metrics.cpp
    src/enet_host.cpp
    src/enet_client.cpp
    src/world_entity.cpp
    src/replication_protocol.cpp
    src/client_interpolator.cpp
    src/world_replication.cpp
    src/thread_bridge.cpp
    src/mock_simulation.cpp
    src/enet_worker.cpp
)

target_include_directories(pse_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/include
)

target_link_libraries(pse_core PUBLIC
    enet
    ws2_32
    winmm
    psapi
    bcrypt
)

# Standalone Host Executable
add_executable(pse_host src/host_main.cpp)
target_link_libraries(pse_host PRIVATE pse_core)

# Standalone Client Executable
add_executable(pse_client src/client_main.cpp)
target_link_libraries(pse_client PRIVATE pse_core)

# Automated Test Suite Executable
add_executable(pse_test_suite tests/test_suite.cpp)
target_link_libraries(pse_test_suite PRIVATE pse_core)

# Scalability & Latency Benchmark Executable
add_executable(pse_benchmark tests/benchmark_metrics.cpp)
target_link_libraries(pse_benchmark PRIVATE pse_core)

# Thread Bridge Stress & Concurrency Test Executable
add_executable(pse_stress_test tests/stress_test_bridge.cpp)
target_link_libraries(pse_stress_test PRIVATE pse_core)

# Thread Bridge Adversarial Fault Injection Suite Executable
add_executable(pse_fault_injection tests/test_fault_injection.cpp)
target_link_libraries(pse_fault_injection PRIVATE pse_core)


# Thread Bridge P0 End-to-End Acceptance Suite (DOC-40)
add_executable(pse_bridge_acceptance tests/test_bridge_acceptance.cpp)
target_link_libraries(pse_bridge_acceptance PRIVATE pse_core)
