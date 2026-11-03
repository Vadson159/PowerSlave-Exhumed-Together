cmake_minimum_required(VERSION 3.24)
project(pse_m11_native_bootstrap LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This exact-build experiment requires MSVC Windows x64")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(m11_minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde64.c)
target_include_directories(m11_minhook PUBLIC third_party/minhook/include)
add_library(pse_m11 SHARED src/bootstrap.cpp)
target_compile_features(pse_m11 PRIVATE cxx_std_20)
target_compile_definitions(pse_m11 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(pse_m11 PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
target_link_libraries(pse_m11 PRIVATE m11_minhook bcrypt)
target_link_options(pse_m11 PRIVATE /INCREMENTAL:NO /DEBUG:FULL /OPT:REF /OPT:ICF)
set_target_properties(pse_m11 PROPERTIES OUTPUT_NAME pse_m11_bootstrap)
