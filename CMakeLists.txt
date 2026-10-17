cmake_minimum_required(VERSION 3.27)
project(Epipen VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Epipen currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(FXC_EXECUTABLE fxc
  HINTS "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
        "$ENV{WindowsSdkDir}/bin/10.0.26100.0/x64")
if(NOT FXC_EXECUTABLE)
  message(FATAL_ERROR "fxc.exe was not found in the installed Windows SDK.")
endif()

add_executable(epipen WIN32
  src/main.cpp
  src/monitor_catalog.cpp
  src/settings_store.cpp
  src/overlay.cpp
  src/capture_session.cpp
  src/filter_math.cpp
  src/filter_pipeline.cpp
  src/diagnostics.cpp
)

target_include_directories(epipen PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(epipen PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(epipen PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
target_link_libraries(epipen PRIVATE d3d11 dxgi dxguid dcomp windowsapp dwmapi user32 gdi32 shell32 comctl32 shcore wtsapi32)
target_compile_options(epipen PRIVATE $<$<CONFIG:Debug>:/Zi>)

set(SHADER_COMPUTE "${CMAKE_CURRENT_BINARY_DIR}/generated/filter_compute.h")
set(SHADER_VS "${CMAKE_CURRENT_BINARY_DIR}/generated/filter_vs.h")
set(SHADER_PS "${CMAKE_CURRENT_BINARY_DIR}/generated/filter_ps.h")
add_custom_command(OUTPUT "${SHADER_COMPUTE}" "${SHADER_VS}" "${SHADER_PS}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
  COMMAND "${FXC_EXECUTABLE}" /nologo /T cs_5_0 /E FilterCS /Fh "${SHADER_COMPUTE}" /Vn g_filterCompute "${CMAKE_CURRENT_SOURCE_DIR}/shaders/filter.hlsl"
  COMMAND "${FXC_EXECUTABLE}" /nologo /T vs_5_0 /E VSMain /Fh "${SHADER_VS}" /Vn g_filterVS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/filter.hlsl"
  COMMAND "${FXC_EXECUTABLE}" /nologo /T ps_5_0 /E PSMain /Fh "${SHADER_PS}" /Vn g_filterPS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/filter.hlsl"
  DEPENDS shaders/filter.hlsl VERBATIM)
add_custom_target(epipen_shader DEPENDS "${SHADER_COMPUTE}" "${SHADER_VS}" "${SHADER_PS}")
add_dependencies(epipen epipen_shader)

add_executable(epipen_background WIN32 tests/background_probe.cpp)
target_compile_definitions(epipen_background PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(epipen_background PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(epipen_background PRIVATE user32 gdi32)

include(CTest)
if(BUILD_TESTING)
  add_executable(epipen_filter_tests tests/filter_math_tests.cpp src/filter_math.cpp)
  target_include_directories(epipen_filter_tests PRIVATE src)
  target_compile_definitions(epipen_filter_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(epipen_filter_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
  add_test(NAME filter_math COMMAND epipen_filter_tests)

  add_executable(epipen_gpu_tests
    tests/filter_gpu_tests.cpp
    src/filter_math.cpp
    src/filter_pipeline.cpp)
  target_include_directories(epipen_gpu_tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(epipen_gpu_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(epipen_gpu_tests PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)
  target_link_libraries(epipen_gpu_tests PRIVATE d3d11 dxgi)
  add_dependencies(epipen_gpu_tests epipen_shader)
  add_test(NAME filter_gpu COMMAND epipen_gpu_tests)
endif()
