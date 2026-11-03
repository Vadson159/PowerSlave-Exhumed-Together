cmake_minimum_required(VERSION 3.20)
project(M12aTestClient LANGUAGES C CXX)

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

set(ENET_PROTOTYPE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../enet-prototype")

# Official ENet library from existing enet-prototype third_party
add_subdirectory("${ENET_PROTOTYPE_DIR}/third_party/enet" "${CMAKE_CURRENT_BINARY_DIR}/enet_build" EXCLUDE_FROM_ALL)
target_include_directories(enet PUBLIC "${ENET_PROTOTYPE_DIR}/third_party/enet/include")
target_link_libraries(enet PUBLIC ws2_32 winmm)

# Core networking & protocol from existing prototype sources (read-only reuse)
add_library(m12a_core STATIC
    "${ENET_PROTOTYPE_DIR}/src/net_protocol.cpp"
    "${ENET_PROTOTYPE_DIR}/src/metrics.cpp"
    "${ENET_PROTOTYPE_DIR}/src/net_simulator.cpp"
    "${ENET_PROTOTYPE_DIR}/src/simulation.cpp"
    "${ENET_PROTOTYPE_DIR}/src/enet_client.cpp"
    "${ENET_PROTOTYPE_DIR}/src/enet_host.cpp"
)

target_include_directories(m12a_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${ENET_PROTOTYPE_DIR}/include"
    "${ENET_PROTOTYPE_DIR}/third_party/enet/include"
)

target_link_libraries(m12a_core PUBLIC
    enet
    ws2_32
    winmm
    psapi
    bcrypt
)

# Test Client Executable
add_executable(m12a_test_client
    src/main.cpp
    src/m12a_client.cpp
    src/m12a_logger.cpp
    src/m12a_scenarios.cpp
    src/m12a_report.cpp
)

target_include_directories(m12a_test_client PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${ENET_PROTOTYPE_DIR}/include"
    "${ENET_PROTOTYPE_DIR}/third_party/enet/include"
)

target_link_libraries(m12a_test_client PRIVATE
    m12a_core
)
