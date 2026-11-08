cmake_minimum_required(VERSION 3.24)
project(pse_together LANGUAGES C CXX)
# One code base, one DLL: pse_together.dll serves both roles (PSE_ROLE=host|client, see src/native/dll_main.cpp).
# Keep the build tree short (b60/ in the mod root): MSBuild hits MAX_PATH otherwise.
#   cmake -S experiments/pse_together -B b60 -G "Visual Studio 17 2022" -A x64
#   cmake --build b60 --config Release --target pse_together
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "pse_together is an exact-build Windows x64 MSVC project")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
enable_testing()

set(PSE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(PSE_MOD_ROOT "${PSE_ROOT}/../.." ABSOLUTE)
set(PSE_TRANSPORT "${PSE_MOD_ROOT}/tools/enet-prototype")
set(PSE_RUN_DIR "${CMAKE_BINARY_DIR}/testrun")

# Only the vendored ENet library is taken from the old prototype tree; its own targets are never built here.
add_subdirectory("${PSE_TRANSPORT}" transport EXCLUDE_FROM_ALL)

set(PSE_STRICT /W4 /WX /permissive- /EHsc /utf-8)
add_library(pse_headers INTERFACE)
target_include_directories(pse_headers INTERFACE include src src/net/include src/session src/native src/proxy)
target_compile_definitions(pse_headers INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)

# --- vendor: MinHook (BSD-2) ---------------------------------------------------------------------------------------
add_library(pse_minhook STATIC
  third_party/minhook/src/buffer.c third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde64.c)
target_include_directories(pse_minhook PUBLIC third_party/minhook/include)

# --- network: the single protocol (src/net, version in net_protocol.h), worker thread, ENet ------------------------
file(GLOB PSE_NET_SOURCES CONFIGURE_DEPENDS "${PSE_ROOT}/src/net/src/*.cpp")
add_library(pse_net STATIC ${PSE_NET_SOURCES})
target_include_directories(pse_net PUBLIC src/net/include src)
target_compile_features(pse_net PUBLIC cxx_std_20)
target_compile_options(pse_net PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(pse_net PUBLIC enet ws2_32 winmm psapi bcrypt)
target_compile_definitions(pse_net PUBLIC PSE_FEATURE_P4)

# --- host-side session adapter -----------------------------------------------------------------------------------
add_library(pse_session STATIC src/session/network_input.cpp)
target_include_directories(pse_session PUBLIC src/session)
target_link_libraries(pse_session PUBLIC pse_net pse_headers)
target_compile_options(pse_session PRIVATE ${PSE_STRICT})

# --- the DLL -----------------------------------------------------------------------------------------------------
add_library(pse_together SHARED
  src/native/dll_main.cpp src/native/host.cpp src/native/client.cpp
  src/native/native_proxy.cpp src/native/self_native_p16.cpp
  src/native/weapon_pickup_p16.cpp src/native/ui_native.cpp)
target_compile_features(pse_together PRIVATE cxx_std_20)
target_compile_definitions(pse_together PRIVATE UNICODE _UNICODE)
target_compile_options(pse_together PRIVATE ${PSE_STRICT})
target_link_options(pse_together PRIVATE /INCREMENTAL:NO /DEBUG:FULL /OPT:REF /OPT:ICF)
target_link_libraries(pse_together PRIVATE pse_minhook pse_session pse_net pse_headers bcrypt d3d11 dxgi d3dcompiler gdi32 user32)
# Role-specific feature switches are per translation unit (include/pse_together/config.hpp checks them).
set_source_files_properties(src/native/host.cpp PROPERTIES COMPILE_DEFINITIONS
  "PSE_ROLE_HOST;PSE_FEATURE_SNAPSHOT;PSE_FEATURE_MAPREADY;PSE_FEATURE_P4;PSE_FEATURE_P7")
set_source_files_properties(src/native/client.cpp src/native/native_proxy.cpp PROPERTIES COMPILE_DEFINITIONS
  "PSE_ROLE_CLIENT;PSE_FEATURE_SNAPSHOT;PSE_FEATURE_MAPREADY;PSE_FEATURE_P4;PSE_FEATURE_VISUAL_PROXY")
set_source_files_properties(src/native/self_native_p16.cpp PROPERTIES COMPILE_DEFINITIONS
  "PSE_ROLE_CLIENT;PSE_FEATURE_SNAPSHOT;PSE_FEATURE_MAPREADY;PSE_FEATURE_P4")
set_source_files_properties(src/native/weapon_pickup_p16.cpp PROPERTIES COMPILE_DEFINITIONS "PSE_ROLE_HOST;PSE_FEATURE_P4;PSE_FEATURE_P7")
set_target_properties(pse_together PROPERTIES OUTPUT_NAME pse_together
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/dll"
  PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/dll"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")


# --- offline tests: protocol/session/lifecycle contracts that guard what the DLL really uses -----------------------
function(pse_suite name source)
  cmake_parse_arguments(S "" "" "LIBS" ${ARGN})
  add_executable(${name} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(${name} PRIVATE ${S_LIBS})
  target_compile_options(${name} PRIVATE ${PSE_STRICT})
  file(MAKE_DIRECTORY "${PSE_RUN_DIR}/${name}/docs")
  add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}> WORKING_DIRECTORY "${PSE_RUN_DIR}/${name}")
  set_tests_properties(${name} PROPERTIES TIMEOUT 600)
  set(PSE_TESTS ${PSE_TESTS} ${name} PARENT_SCOPE)
endfunction()
set(PSE_TESTS "")
pse_suite(self_state_contract tests/session/self_state_p7.cpp LIBS pse_headers)
pse_suite(respawn_contract tests/session/respawn_p16.cpp LIBS pse_net pse_headers)
pse_suite(respawn_enet tests/session/respawn_enet_p16.cpp LIBS pse_net pse_headers)
pse_suite(weapon_pickup_contract tests/native/weapon_pickup_p16.cpp LIBS pse_headers)
target_sources(weapon_pickup_contract PRIVATE src/native/weapon_pickup_p16.cpp)
target_compile_definitions(weapon_pickup_contract PRIVATE PSE_P16_OFFLINE)
pse_suite(proxy58b_contract tests/proxy/proxy58b_contract.cpp LIBS pse_net pse_headers)
pse_suite(lifecycle58b1_contract tests/proxy/lifecycle58b1_contract.cpp LIBS pse_net pse_headers)
pse_suite(lifecycle58b2_contract tests/proxy/lifecycle58b2_contract.cpp LIBS pse_net pse_headers)
pse_suite(bounded_proxy_contract tests/proxy/bounded_proxy_contract.cpp LIBS pse_headers)
pse_suite(p4_presentation_contract tests/proxy/p4_presentation_contract.cpp LIBS pse_net pse_headers)
pse_suite(ui_contract tests/ui/contract.cpp LIBS pse_net pse_headers)
pse_suite(lobby_live tests/ui/lobby_live.cpp LIBS pse_net pse_headers)
# lobby_live also exercises a non-loopback interface: use the first assigned preferred IPv4 (no remote peer, no firewall change).
execute_process(COMMAND powershell -NoProfile -Command
  "Get-NetIPAddress -AddressFamily IPv4 -AddressState Preferred | Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254.*' } | Select-Object -First 1 -ExpandProperty IPAddress"
  OUTPUT_VARIABLE PSE_LOCAL_IP OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE PSE_IP_RESULT)
if(PSE_IP_RESULT EQUAL 0 AND PSE_LOCAL_IP MATCHES "^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$")
  set_tests_properties(lobby_live PROPERTIES ENVIRONMENT "P23_TEST_LOCAL_IP=${PSE_LOCAL_IP}")
endif()
add_custom_target(pse_tests DEPENDS ${PSE_TESTS})

# --- loader (SDL2 shim v2, broker, pe-play) ----------------------------------------------------------------------
add_custom_target(pse_all DEPENDS pse_together)
add_subdirectory(src/loader)
