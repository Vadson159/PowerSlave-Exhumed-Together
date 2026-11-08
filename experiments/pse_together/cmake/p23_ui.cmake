# P23 is a separate v1.13 target. Frozen v1.10/v1.11/v1.12 remain byte-identical.
# Copied headers isolate quoted-include resolution from the original v1.12 dir.
set(PSE_P23_SOURCES "")
foreach(source IN LISTS PSE_P7_V110_SOURCES)
  get_filename_component(name "${source}" NAME)
  if(name MATCHES "^(net_protocol|enet_host|enet_client|enet_worker)\\.cpp$")
    list(APPEND PSE_P23_SOURCES "${PSE_ROOT}/src/wire113/src/${name}")
  else()
    list(APPEND PSE_P23_SOURCES "${PSE_ROOT}/src/wire112/${source}")
  endif()
endforeach()
add_library(pse_core_p23 STATIC ${PSE_P23_SOURCES})
target_include_directories(pse_core_p23 PUBLIC "${PSE_ROOT}/src/wire113/include" "${PSE_ROOT}/src")
target_compile_features(pse_core_p23 PUBLIC cxx_std_20)
target_compile_options(pse_core_p23 PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(pse_core_p23 PUBLIC enet ws2_32 winmm psapi bcrypt)
target_compile_definitions(pse_core_p23 PUBLIC PSE_FEATURE_P4)

add_library(pse_session_p23 STATIC src/native/p23/network_input_p23.cpp)
target_link_libraries(pse_session_p23 PUBLIC pse_core_p23 pse_headers)
target_compile_options(pse_session_p23 PRIVATE ${PSE_STRICT})
pse_dll(pse_dll_host_p23 src/native/p23/host_p23.cpp pse_m11_bootstrap PSE_ROLE_HOST PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_P4 PSE_FEATURE_P7)
target_sources(pse_dll_host_p23 PRIVATE src/native/p23/ui_native.cpp)
target_link_libraries(pse_dll_host_p23 PRIVATE pse_minhook pse_session_p23 pse_headers bcrypt d3d11 dxgi d3dcompiler gdi32 user32)
pse_dll(pse_dll_client_p23 src/native/p23/client_p23.cpp pse_native_client58b PSE_ROLE_CLIENT PSE_FEATURE_SNAPSHOT PSE_FEATURE_MAPREADY PSE_FEATURE_P4)
target_sources(pse_dll_client_p23 PRIVATE src/native/p23/ui_native.cpp)
target_link_libraries(pse_dll_client_p23 PRIVATE pse_minhook pse_core_p23 pse_headers bcrypt d3d11 dxgi d3dcompiler gdi32 user32)
add_dependencies(pse_dlls pse_dll_host_p23 pse_dll_client_p23)
pse_suite(p23_d3d_fixture tests/ui/d3d_fixture.cpp LIBS pse_core_p23 pse_headers pse_minhook d3d11 dxgi d3dcompiler gdi32 user32 ARGS "${PSE_MOD_ROOT}/experiments/m0/game/SDL2.dll")
target_sources(p23_d3d_fixture PRIVATE src/native/p23/ui_native.cpp)
target_compile_definitions(p23_d3d_fixture PRIVATE PSE_P23_UI_FIXTURE)
pse_suite(p23_ui_contract tests/ui/contract.cpp LIBS pse_core_p23 pse_headers)
pse_suite(p23_lobby_live tests/ui/lobby_live.cpp LIBS pse_core_p23 pse_headers)
# Resolve an actually assigned Preferred IPv4 for one-PC LAN-interface smoke.
# No game, public endpoint, firewall change, or remote peer is involved.
execute_process(COMMAND powershell -NoProfile -Command
  "Get-NetIPAddress -AddressFamily IPv4 -AddressState Preferred | Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254.*' } | Select-Object -First 1 -ExpandProperty IPAddress"
  OUTPUT_VARIABLE PSE_P23_LOCAL_IP OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE PSE_P23_IP_RESULT)
if(PSE_P23_IP_RESULT EQUAL 0 AND PSE_P23_LOCAL_IP MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
  set_tests_properties(p23_lobby_live PROPERTIES ENVIRONMENT "P23_TEST_LOCAL_IP=${PSE_P23_LOCAL_IP}")
else()
  message(WARNING "P23 non-loopback IPv4 unavailable; p23_lobby_live will report the missing gate")
endif()
