cmake_minimum_required(VERSION 3.20)
project(pse_remediation_qa LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype" transport EXCLUDE_FROM_ALL)

# Target 1: Comparative Test Suite (18 Scenarios)
add_executable(test_remediation_comparative test_remediation_comparative.cpp)
target_include_directories(test_remediation_comparative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/third_party/enet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_snapshot/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_map_ready/src
)
target_link_libraries(test_remediation_comparative PRIVATE
    pse_core
    ws2_32
    winmm
    bcrypt
)
target_compile_options(test_remediation_comparative PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(test_remediation_comparative PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Target 2: Reproduction ADV-02
add_executable(repro_adv02_failed_remove repro_adv02_failed_remove.cpp)
target_include_directories(repro_adv02_failed_remove PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/third_party/enet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_snapshot/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_map_ready/src
)
target_link_libraries(repro_adv02_failed_remove PRIVATE
    pse_core
    ws2_32
    winmm
    bcrypt
)
target_compile_options(repro_adv02_failed_remove PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(repro_adv02_failed_remove PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Target 3: Reproduction ADV-11
add_executable(repro_adv11_sector_fallacy repro_adv11_sector_fallacy.cpp)
target_include_directories(repro_adv11_sector_fallacy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/third_party/enet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_snapshot/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_map_ready/src
)
target_link_libraries(repro_adv11_sector_fallacy PRIVATE
    pse_core
    ws2_32
    winmm
    bcrypt
)
target_compile_options(repro_adv11_sector_fallacy PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(repro_adv11_sector_fallacy PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
