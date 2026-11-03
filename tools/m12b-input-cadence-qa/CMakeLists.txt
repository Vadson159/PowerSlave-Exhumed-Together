cmake_minimum_required(VERSION 3.20)
project(M12bInputCadenceQA LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

set(ENET_PROTOTYPE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../enet-prototype")

add_subdirectory("${ENET_PROTOTYPE_DIR}/third_party/enet" "${CMAKE_CURRENT_BINARY_DIR}/enet_build" EXCLUDE_FROM_ALL)
target_include_directories(enet PUBLIC "${ENET_PROTOTYPE_DIR}/third_party/enet/include")
target_link_libraries(enet PUBLIC ws2_32 winmm)

# 1. Scheduler Benchmark
add_executable(scheduler_bench src/scheduler_bench.cpp)
target_include_directories(scheduler_bench PRIVATE src)
target_link_libraries(scheduler_bench PRIVATE winmm)

# 2. Cadence QA Host
add_executable(cadence_qa_host
    src/cadence_qa_host.cpp
    "${ENET_PROTOTYPE_DIR}/src/net_protocol.cpp"
    "${ENET_PROTOTYPE_DIR}/src/replication_protocol.cpp"
)
target_include_directories(cadence_qa_host PRIVATE
    src
    "${ENET_PROTOTYPE_DIR}/include"
    "${ENET_PROTOTYPE_DIR}/third_party/enet/include"
)
target_link_libraries(cadence_qa_host PRIVATE enet ws2_32 winmm)

# 3. Precision Client
add_executable(precision_client
    src/precision_client.cpp
    "${ENET_PROTOTYPE_DIR}/src/net_protocol.cpp"
    "${ENET_PROTOTYPE_DIR}/src/replication_protocol.cpp"
)
target_include_directories(precision_client PRIVATE
    src
    "${ENET_PROTOTYPE_DIR}/include"
    "${ENET_PROTOTYPE_DIR}/third_party/enet/include"
)
target_link_libraries(precision_client PRIVATE enet ws2_32 winmm)

# 4. Net Impairment Proxy
add_executable(net_impairment_proxy src/net_impairment_proxy.cpp)
target_include_directories(net_impairment_proxy PRIVATE src)
target_link_libraries(net_impairment_proxy PRIVATE ws2_32 winmm)
