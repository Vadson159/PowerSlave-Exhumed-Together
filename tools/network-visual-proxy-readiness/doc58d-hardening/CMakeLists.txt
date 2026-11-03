cmake_minimum_required(VERSION 3.20)
project(pse_doc58d_hardening LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype" transport EXCLUDE_FROM_ALL)

set(COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../remediation
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/third_party/enet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_snapshot/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_map_ready/src
)

set(COMMON_LIBS
    pse_core
    ws2_32
    winmm
    bcrypt
)

# Target 1: Full 70-Scenario Regression & Adversarial QA Suite
add_executable(test_doc58d_regression test_doc58d_regression.cpp)
target_include_directories(test_doc58d_regression PRIVATE ${COMMON_INCLUDES})
target_link_libraries(test_doc58d_regression PRIVATE ${COMMON_LIBS})
target_compile_options(test_doc58d_regression PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(test_doc58d_regression PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Target 2: Reentrancy Defect (BEFORE Fix - DOC-58B Candidate)
add_executable(repro_reentrancy_before repro_reentrancy_before.cpp)
target_include_directories(repro_reentrancy_before PRIVATE ${COMMON_INCLUDES})
target_link_libraries(repro_reentrancy_before PRIVATE ${COMMON_LIBS})
target_compile_options(repro_reentrancy_before PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(repro_reentrancy_before PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Target 3: Reentrancy Guard (AFTER Fix - DOC-58D Hardened Candidate)
add_executable(repro_reentrancy_after repro_reentrancy_after.cpp)
target_include_directories(repro_reentrancy_after PRIVATE ${COMMON_INCLUDES})
target_link_libraries(repro_reentrancy_after PRIVATE ${COMMON_LIBS})
target_compile_options(repro_reentrancy_after PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(repro_reentrancy_after PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Target 4: 3-8 Player Slot Limit Defect (BEFORE Fix - DOC-58B Candidate)
add_executable(repro_slot_limit_before repro_slot_limit_before.cpp)
target_include_directories(repro_slot_limit_before PRIVATE ${COMMON_INCLUDES})
target_link_libraries(repro_slot_limit_before PRIVATE ${COMMON_LIBS})
target_compile_options(repro_slot_limit_before PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(repro_slot_limit_before PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Target 5: MultiSlotGuard8 (AFTER Fix - DOC-58D Hardened Candidate)
add_executable(repro_slot_limit_after repro_slot_limit_after.cpp)
target_include_directories(repro_slot_limit_after PRIVATE ${COMMON_INCLUDES})
target_link_libraries(repro_slot_limit_after PRIVATE ${COMMON_LIBS})
target_compile_options(repro_slot_limit_after PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(repro_slot_limit_after PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
