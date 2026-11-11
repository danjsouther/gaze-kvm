cmake_minimum_required(VERSION 3.21)
project(gazekvm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# --- Pure decision logic: no Win32, unit-tested ------------------------------
add_library(gazekvm_engine STATIC src/engine.cpp)
target_include_directories(gazekvm_engine PUBLIC src)

# --- Win32 core shared by both front ends ------------------------------------
add_library(gazekvm_core STATIC
  src/config.cpp
  src/switcher.cpp
  src/input_hooks.cpp
  src/tobii_source.cpp
  src/core_source.cpp
  src/runner.cpp
)
target_compile_definitions(gazekvm_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(gazekvm_core PUBLIC gazekvm_engine user32 ws2_32)

# gazekvm.exe: console (run, --debug, --calibrate, tests)
add_executable(gazekvm src/main.cpp)
target_link_libraries(gazekvm PRIVATE gazekvm_core)

# gazekvmw.exe: system tray, no console
add_executable(gazekvmw WIN32 src/tray_main.cpp)
target_link_libraries(gazekvmw PRIVATE gazekvm_core shell32 gdi32)

# gazekvm-tobii.exe: .NET helper that reads Tobii.Interaction (the Tobii Core
# SDK, from NuGet) for the "core" source. Built with the dotnet CLI into the
# same folder as the exes.
find_program(DOTNET dotnet)
if(DOTNET)
  add_custom_target(gazekvm_tobii ALL
    COMMAND ${DOTNET} build "${CMAKE_SOURCE_DIR}/tools/gazekvm-tobii/gazekvm-tobii.csproj"
            -c Release --nologo -v q -o "$<TARGET_FILE_DIR:gazekvm>"
    COMMENT "Building gazekvm-tobii (Tobii.Interaction helper)"
    VERBATIM)
  add_dependencies(gazekvm_tobii gazekvm)
else()
  message(WARNING "dotnet not found: gazekvm-tobii.exe (the 'core' source) won't be built.")
endif()

# --- Tobii Game Integration SDK (optional until it arrives) ------------------
# Unpack the SDK into third_party/tobii_gameintegration_<version>/ (gitignored).
# Without it, gazekvm still builds: --test-switch / --test-edge work, and the
# Tobii-dependent modes report that the SDK is missing.
file(GLOB TGI_DIRS LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/third_party/tobii_gameintegration_*")
set(TGI_FOUND OFF)
if(TGI_DIRS)
  list(SORT TGI_DIRS)
  list(GET TGI_DIRS -1 TGI_ROOT)
  file(GLOB_RECURSE TGI_HEADER "${TGI_ROOT}/*tobii_gameintegration.h")
  file(GLOB_RECURSE TGI_LIB    "${TGI_ROOT}/*tobii_gameintegration_x64.lib")
  file(GLOB_RECURSE TGI_DLL    "${TGI_ROOT}/*tobii_gameintegration_x64.dll")
  if(TGI_HEADER AND TGI_LIB AND TGI_DLL)
    list(GET TGI_HEADER 0 TGI_HEADER)
    list(GET TGI_LIB 0 TGI_LIB)
    list(GET TGI_DLL 0 TGI_DLL)
    get_filename_component(TGI_INCLUDE_DIR "${TGI_HEADER}" DIRECTORY)
    target_include_directories(gazekvm_core PRIVATE "${TGI_INCLUDE_DIR}")
    target_link_libraries(gazekvm_core PUBLIC "${TGI_LIB}")
    target_compile_definitions(gazekvm_core PRIVATE GAZEKVM_HAVE_TGI=1)
    add_custom_command(TARGET gazekvm POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TGI_DLL}" "$<TARGET_FILE_DIR:gazekvm>")
    set(TGI_FOUND ON)
    message(STATUS "Tobii Game Integration SDK: ${TGI_ROOT}")
  else()
    message(WARNING "Found ${TGI_ROOT} but not header/lib/dll inside it; building without Tobii.")
  endif()
endif()
if(NOT TGI_FOUND)
  message(STATUS "Tobii Game Integration SDK not found; building without Tobii support.")
endif()

# Seed a default config next to the exe on first build (never overwrites an
# existing one, so calibration survives rebuilds).
add_custom_command(TARGET gazekvm POST_BUILD
  COMMAND ${CMAKE_COMMAND}
    -DSRC=${CMAKE_SOURCE_DIR}/config/gazekvm.ini
    -DDST=$<TARGET_FILE_DIR:gazekvm>/gazekvm.ini
    -P ${CMAKE_SOURCE_DIR}/cmake/copy_if_missing.cmake)

# --- Tests ------------------------------------------------------------------
enable_testing()
add_executable(engine_tests tests/engine_tests.cpp)
target_link_libraries(engine_tests PRIVATE gazekvm_engine)
add_test(NAME engine_tests COMMAND engine_tests)
