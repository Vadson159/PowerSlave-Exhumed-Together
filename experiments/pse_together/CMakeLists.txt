cmake_minimum_required(VERSION 3.24)
project(pse_together LANGUAGES C CXX)
# S0.3 unified code base: roles (host/client), feature flags (include/pse_together/config.hpp), every offline
# contract suite of the M1 laboratories (DOC-44..58B.2) in one tree, one `ctest`.
# Build tree must be SHORT (b60/ in the mod root): MSBuild hits MAX_PATH otherwise (ROADMAP appendix A).
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
set(PSE_LEGACY_M1 "${PSE_MOD_ROOT}/experiments/m1")
set(PSE_RUN_DIR "${CMAKE_BINARY_DIR}/testrun")

# Transport core (pse_core: Wire v1.10, bridge, worker, ENet) is shared unchanged; its own suites are registered below.
add_subdirectory("${PSE_TRANSPORT}" transport EXCLUDE_FROM_ALL)

set(PSE_STRICT /W4 /WX /permissive- /EHsc /utf-8)
add_library(pse_headers INTERFACE)
target_include_directories(pse_headers INTERFACE include src/session src/native src/proxy)
target_compile_definitions(pse_headers INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)

# --- vendor: MinHook (BSD-2), unchanged copy of experiments/m1/native_bootstrap/third_party/minhook ---------------
add_library(pse_minhook STATIC
  third_party/minhook/src/buffer.c third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde64.c)
target_include_directories(pse_minhook PUBLIC third_party/minhook/include)

# --- session adapter: host-side network session (DOC-44/46/47/49/50/53/55/56) ---------------------------------------
add_library(pse_session STATIC src/session/network_input.cpp)
target_include_directories(pse_session PUBLIC src/session)
target_link_libraries(pse_session PUBLIC pse_core pse_headers)
target_compile_features(pse_session PUBLIC cxx_std_20)
target_compile_options(pse_session PRIVATE ${PSE_STRICT})

# --- DLL targets ------------------------------------------------------------------------------------------------------
function(pse_dll target source output)
  add_library(${target} SHARED ${source})
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE ${ARGN})
  target_compile_options(${target} PRIVATE ${PSE_STRICT})
  target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG:FULL /OPT:REF /OPT:ICF)
  set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output}
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${target}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/dll/${target}"
    PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/dll/${target}")
endfunction()
# Host, role HOST: frozen DOC-56 chain (loader/export surface keeps the name pse_m11_bootstrap).
pse_dll(pse_dll_host src/native/host.cpp pse_m11_bootstrap PSE_ROLE_HOST PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY)
target_link_libraries(pse_dll_host PRIVATE pse_minhook pse_session pse_headers bcrypt)
# Host without network: the plain DOC-41 bootstrap (no role flag).
pse_dll(pse_dll_host_bootstrap src/native/host.cpp pse_m11_bootstrap)
target_link_libraries(pse_dll_host_bootstrap PRIVATE pse_minhook pse_headers bcrypt)
# Client, role CLIENT: DOC-56 client (no proxy) and DOC-58B client (visual proxy seam for the Frida adapter).
pse_dll(pse_dll_client src/native/client.cpp pse_native_client PSE_ROLE_CLIENT PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY)
pse_dll(pse_dll_client_proxy src/native/client.cpp pse_native_client58b PSE_ROLE_CLIENT PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_VISUAL_PROXY)
target_sources(pse_dll_client_proxy PRIVATE src/native/native_proxy.cpp)
foreach(t pse_dll_client pse_dll_client_proxy)
  target_link_libraries(${t} PRIVATE pse_minhook pse_core pse_headers bcrypt)
endforeach()
# P4 candidates are separate targets; accepted P1 variants retain their compile-time behavior.
get_target_property(PSE_V110_SOURCES pse_core SOURCES)
set(PSE_V111_SOURCES "")
foreach(source IN LISTS PSE_V110_SOURCES)
  list(APPEND PSE_V111_SOURCES "${PSE_ROOT}/src/wire111/${source}")
endforeach()
add_library(pse_core_p4 STATIC ${PSE_V111_SOURCES})
target_include_directories(pse_core_p4 PUBLIC src/wire111/include)
target_compile_definitions(pse_core_p4 PUBLIC PSE_FEATURE_P4)
target_compile_options(pse_core_p4 PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(pse_core_p4 PUBLIC enet ws2_32 winmm psapi bcrypt)
add_library(pse_session_p4 STATIC src/session/network_input.cpp)
target_include_directories(pse_session_p4 PUBLIC src/session)
target_link_libraries(pse_session_p4 PUBLIC pse_core_p4 pse_headers)
target_compile_options(pse_session_p4 PRIVATE ${PSE_STRICT})
pse_dll(pse_dll_host_p4 src/native/host.cpp pse_m11_bootstrap PSE_ROLE_HOST PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_P4)
target_link_libraries(pse_dll_host_p4 PRIVATE pse_minhook pse_session_p4 pse_headers bcrypt)
pse_dll(pse_dll_client_p4 src/native/client.cpp pse_native_client58b PSE_ROLE_CLIENT PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_VISUAL_PROXY PSE_FEATURE_P4)
target_sources(pse_dll_client_p4 PRIVATE src/native/native_proxy.cpp)
target_link_libraries(pse_dll_client_p4 PRIVATE pse_minhook pse_core_p4 pse_headers bcrypt)
add_custom_target(pse_dlls DEPENDS pse_dll_host pse_dll_host_bootstrap pse_dll_client pse_dll_client_proxy pse_dll_host_p4 pse_dll_client_p4)

# P14 is isolated from accepted P1 and pending P4 artifacts.
pse_dll(pse_dll_host_p14 src/native/host.cpp pse_m11_bootstrap PSE_ROLE_HOST PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_P14)
target_sources(pse_dll_host_p14 PRIVATE src/native/gameplay_p14.cpp)
target_link_libraries(pse_dll_host_p14 PRIVATE pse_minhook pse_session pse_headers bcrypt)
add_dependencies(pse_dlls pse_dll_host_p14)

# P8 art binding candidate. No P4/P14 flags; old target source paths unchanged.
pse_dll(pse_dll_host_p8 src/native/host.cpp pse_m11_bootstrap PSE_ROLE_HOST PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_P8)
target_link_libraries(pse_dll_host_p8 PRIVATE pse_minhook pse_session pse_headers bcrypt)
pse_dll(pse_dll_client_p8 src/native/client.cpp pse_native_client58b PSE_ROLE_CLIENT PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_VISUAL_PROXY PSE_FEATURE_P8)
target_sources(pse_dll_client_p8 PRIVATE src/native/native_proxy.cpp)
target_link_libraries(pse_dll_client_p8 PRIVATE pse_minhook pse_core pse_headers bcrypt)
add_dependencies(pse_dlls pse_dll_host_p8 pse_dll_client_p8)

# P3 reconnect/rearm candidate (DOC-71): separate fork files; accepted P1 targets and their sources are unchanged.
pse_dll(pse_dll_client_p3 src/native/client_p3.cpp pse_native_client58b PSE_ROLE_CLIENT PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_VISUAL_PROXY PSE_FEATURE_P3)
target_sources(pse_dll_client_p3 PRIVATE src/native/native_proxy_p3.cpp)
target_link_libraries(pse_dll_client_p3 PRIVATE pse_minhook pse_core pse_headers bcrypt)
add_dependencies(pse_dlls pse_dll_client_p3)

# --- standalone loopback client of the session adapter (was m12a_native_client) ------------------------------------
add_executable(pse_session_client src/tools/session_client_main.cpp)
target_link_libraries(pse_session_client PRIVATE pse_core)
target_compile_options(pse_session_client PRIVATE /W4 /WX /EHsc /utf-8)

# --- offline suites ---------------------------------------------------------------------------------------------------
set(PSE_SUITE_TARGETS "")
set(PSE_TEST_NAMES "")
# pse_suite(name source [LIBS ...] [OPTIONS ...] [INCLUDES ...] [ARGS ...])
function(pse_suite name source)
  cmake_parse_arguments(S "" "" "LIBS;OPTIONS;INCLUDES;ARGS" ${ARGN})
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE ${S_LIBS})
  if(S_INCLUDES)
    target_include_directories(${name} PRIVATE ${S_INCLUDES})
  endif()
  if(S_OPTIONS)
    target_compile_options(${name} PRIVATE ${S_OPTIONS})
  else()
    target_compile_options(${name} PRIVATE ${PSE_STRICT})
  endif()
  file(MAKE_DIRECTORY "${PSE_RUN_DIR}/${name}/docs")
  add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}> ${S_ARGS} WORKING_DIRECTORY "${PSE_RUN_DIR}/${name}")
  set(PSE_SUITE_TARGETS ${PSE_SUITE_TARGETS} ${name} PARENT_SCOPE)
  set(PSE_TEST_NAMES ${PSE_TEST_NAMES} ${name} PARENT_SCOPE)
endfunction()

# Transport / thread bridge (tools/enet-prototype): wire v1.10, bridge affinity, fault injection. cwd is a scratch dir:
# pse_test_suite writes docs/TEST_RESULTS.md relative to cwd and must never touch the repository docs/.
foreach(t pse_test_suite pse_stress_test pse_fault_injection pse_bridge_acceptance)
  file(MAKE_DIRECTORY "${PSE_RUN_DIR}/${t}/docs")
  add_test(NAME ${t} COMMAND $<TARGET_FILE:${t}> WORKING_DIRECTORY "${PSE_RUN_DIR}/${t}")
  set(PSE_SUITE_TARGETS ${PSE_SUITE_TARGETS} ${t})
  set(PSE_TEST_NAMES ${PSE_TEST_NAMES} ${t})
endforeach()

# Session adapter (DOC-44/46/47/49/50)
include(cmake/p7_wire112.cmake)
pse_dll(pse_dll_host_p7 src/native/host_p7.cpp pse_m11_bootstrap PSE_ROLE_HOST PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_P4 PSE_FEATURE_P7)
target_link_libraries(pse_dll_host_p7 PRIVATE pse_minhook pse_session_p7 pse_headers bcrypt)
add_dependencies(pse_dlls pse_dll_host_p7)
pse_dll(pse_dll_host_p16 src/native/host_p16.cpp pse_m11_bootstrap PSE_ROLE_HOST PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_P4 PSE_FEATURE_P7)
add_library(pse_session_p16 STATIC src/session/network_input_p16.cpp)
target_include_directories(pse_session_p16 PUBLIC src/session)
target_link_libraries(pse_session_p16 PUBLIC pse_core_p7 pse_headers)
target_compile_options(pse_session_p16 PRIVATE ${PSE_STRICT})
target_sources(pse_dll_host_p16 PRIVATE src/native/weapon_pickup_p16.cpp)
target_link_libraries(pse_dll_host_p16 PRIVATE pse_minhook pse_session_p16 pse_headers bcrypt)
add_dependencies(pse_dlls pse_dll_host_p16)
pse_dll(pse_dll_client_p16 src/native/client_p16.cpp pse_native_client58b PSE_ROLE_CLIENT PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_P4 PSE_FEATURE_P7)
target_sources(pse_dll_client_p16 PRIVATE src/native/self_native_p16.cpp)
target_link_libraries(pse_dll_client_p16 PRIVATE pse_minhook pse_core_p7 pse_headers bcrypt)
add_dependencies(pse_dlls pse_dll_client_p16)
if(EXISTS "${PSE_ROOT}/src/native/self_native_p7.cpp")
pse_dll(pse_dll_client_p7 src/native/client_p7.cpp pse_native_client58b PSE_ROLE_CLIENT PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_P4 PSE_FEATURE_P7)
target_sources(pse_dll_client_p7 PRIVATE src/native/self_native_p7.cpp)
target_link_libraries(pse_dll_client_p7 PRIVATE pse_minhook pse_core_p7 pse_headers bcrypt)
add_dependencies(pse_dlls pse_dll_client_p7)
endif()
pse_suite(session_generation_test tests/session/generation_test.cpp LIBS pse_core pse_headers)
pse_suite(session_adapter_test tests/session/adapter_test.cpp LIBS pse_session)
pse_suite(session_pressure_test tests/session/pressure_test.cpp LIBS pse_session)
pse_suite(routing_contract tests/session/routing_contract.cpp LIBS pse_session)
pse_suite(r1_live tests/session/r1_live.cpp LIBS pse_session)
pse_suite(admission_contract tests/session/admission_contract.cpp LIBS pse_session)
pse_suite(r2_fixed tests/session/r2_fixed.cpp LIBS pse_session ARGS r2)  # no argument = R1 defect reproducer (exit 10 by design)
pse_suite(timeout_contract tests/session/timeout_contract.cpp LIBS pse_session)
# sequence_contract compiles enet_worker.cpp and network_input.cpp into its own TU (white-box access override).
pse_suite(sequence_contract tests/session/sequence_contract.cpp LIBS pse_core pse_headers INCLUDES "${PSE_TRANSPORT}/src")
pse_suite(sequence_live tests/session/sequence_live.cpp LIBS pse_session)
# P7 draft scalar admission only; not connected to either frozen wire or KEX.
pse_suite(p7_self_state_contract tests/session/self_state_p7.cpp LIBS pse_headers)
pse_suite(p16_respawn_contract tests/session/respawn_p16.cpp LIBS pse_core_p7 pse_headers)
pse_suite(p16_respawn_enet tests/session/respawn_enet_p16.cpp LIBS pse_core_p7 pse_headers)
pse_suite(p16_weapon_contract tests/native/weapon_pickup_p16.cpp LIBS pse_headers)
target_sources(p16_weapon_contract PRIVATE src/native/weapon_pickup_p16.cpp)
target_compile_definitions(p16_weapon_contract PRIVATE PSE_P16_OFFLINE)
# Native-side contracts (DOC-53/55/56 + client worker); no KEX, no game
pse_suite(background_contract tests/native/background_contract.cpp LIBS pse_session)
pse_suite(snapshot_contract tests/native/snapshot_contract.cpp LIBS pse_session)
pse_suite(map_ready_contract tests/native/map_ready_contract.cpp LIBS pse_session bcrypt)
pse_suite(native_client_contract tests/native/client_worker_test.cpp LIBS pse_core pse_headers)
# Visual-proxy automaton (DOC-58B, 58B.1, 58B.2): one header, three contract generations
pse_suite(proxy58b_contract tests/proxy/proxy58b_contract.cpp LIBS pse_core pse_headers)
pse_suite(lifecycle58b1_contract tests/proxy/lifecycle58b1_contract.cpp LIBS pse_core pse_headers)
pse_suite(lifecycle58b2_contract tests/proxy/lifecycle58b2_contract.cpp LIBS pse_core pse_headers)
pse_suite(bounded_proxy_contract tests/proxy/bounded_proxy_contract.cpp LIBS pse_headers)
pse_suite(p4_presentation_contract tests/proxy/p4_presentation_contract.cpp LIBS pse_core pse_headers)
pse_suite(p4_wire111_contract tests/native/p4_wire111_contract.cpp LIBS pse_session_p4)
pse_suite(p14_gameplay_contract tests/native/p14_gameplay_contract.cpp LIBS pse_headers)
pse_suite(p8_animation_contract tests/proxy/p8_animation_contract.cpp LIBS pse_core pse_headers)
pse_suite(p3_rearm_contract tests/proxy/p3_rearm_contract.cpp LIBS pse_core pse_headers)
target_sources(p14_gameplay_contract PRIVATE src/native/gameplay_p14.cpp)
target_compile_definitions(p14_gameplay_contract PRIVATE PSE_P14_OFFLINE)
target_link_libraries(p14_gameplay_contract PRIVATE pse_minhook)

# JavaScript adapter/controller offline suites (Frida adapter stays JS until S0.4); run from their own folders.
find_program(PSE_NODE node PATHS "C:/Program Files/nodejs")
if(PSE_NODE)
  add_test(NAME adapter58b_js COMMAND "${PSE_NODE}" offline58b.js WORKING_DIRECTORY "${PSE_LEGACY_M1}/native_network_visual_proxy_58b")
  add_test(NAME controller58a_js COMMAND "${PSE_NODE}" offline_test.js WORKING_DIRECTORY "${PSE_LEGACY_M1}/native_visual_proxy_movement")
  set(PSE_TEST_NAMES ${PSE_TEST_NAMES} adapter58b_js controller58a_js)
else()
  message(WARNING "node not found: JS offline suites are NOT registered (run_offline.py will report them missing)")
endif()

include(cmake/p23_ui.cmake)
add_custom_target(pse_offline_suites DEPENDS ${PSE_SUITE_TARGETS})
add_custom_target(pse_all DEPENDS pse_offline_suites pse_dlls pse_session_client)
add_subdirectory(src/loader)
set_tests_properties(${PSE_TEST_NAMES} PROPERTIES TIMEOUT 600)
