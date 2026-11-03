cmake_minimum_required(VERSION 3.20)
project(pse_adversarial_lifecycle_qa LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype" transport EXCLUDE_FROM_ALL)

add_executable(test_adversarial_lifecycle test_adversarial_lifecycle.cpp)

target_include_directories(test_adversarial_lifecycle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../enet-prototype/third_party/enet/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_snapshot/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../experiments/m1/native_map_ready/src
)

target_link_libraries(test_adversarial_lifecycle PRIVATE
    pse_core
    ws2_32
    winmm
    bcrypt
)

target_compile_options(test_adversarial_lifecycle PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_compile_definitions(test_adversarial_lifecycle PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
