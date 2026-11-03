cmake_minimum_required(VERSION 3.20)
project(pse_doc58c_integration_qa LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype" transport EXCLUDE_FROM_ALL)

# Target 1: Full 32-Scenario Integration Contract QA Suite
add_executable(test_integration_contracts test_integration_contracts.cpp)
target_include_directories(test_integration_contracts PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../remediation
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/third_party/enet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_snapshot/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_map_ready/src
)
target_link_libraries(test_integration_contracts PRIVATE
    pse_core
    ws2_32
    winmm
    bcrypt
)
target_compile_options(test_integration_contracts PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(test_integration_contracts PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Target 2: Dedicated Minimal Reproduction for Reentrancy Hazard
add_executable(repro_reentrancy_hazard repro_reentrancy_hazard.cpp)
target_include_directories(repro_reentrancy_hazard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../remediation
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/third_party/enet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_snapshot/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_map_ready/src
)
target_link_libraries(repro_reentrancy_hazard PRIVATE
    pse_core
    ws2_32
    winmm
    bcrypt
)
target_compile_options(repro_reentrancy_hazard PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(repro_reentrancy_hazard PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Target 3: Dedicated Minimal Reproduction for 3-8 Player Slot Limit Defect
add_executable(repro_snapshot_slot_limit repro_snapshot_slot_limit.cpp)
target_include_directories(repro_snapshot_slot_limit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../remediation
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/third_party/enet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_snapshot/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_map_ready/src
)
target_link_libraries(repro_snapshot_slot_limit PRIVATE
    pse_core
    ws2_32
    winmm
    bcrypt
)
target_compile_options(repro_snapshot_slot_limit PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(repro_snapshot_slot_limit PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
