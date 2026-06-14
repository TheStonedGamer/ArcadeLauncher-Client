# ArcadeLauncher Client — CMake build (Linux native port, Phase L0).
#
# Scope of L0: compile the genuinely portable pieces of the client into an
# `arcade_core` static library on Linux, with NO windows.h, proving the
# toolchain and the platform boundary work. The Windows MSI build still goes
# through scripts/build.ps1 / MSBuild and is untouched by this file. Later
# phases (see PORTING_LINUX.md) grow `arcade_core` and add the SDL2/nanovg/
# libcurl/IXWebSocket/miniaudio app targets.
#
# Build:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/core_selfcheck   # exits 0 on success

cmake_minimum_required(VERSION 3.16)
project(ArcadeLauncherClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── arcade_core ──────────────────────────────────────────────────────────────
# The portable foundation. Files added here must compile clean on Linux with no
# Win32 dependency. As files are ported (PORTING_LINUX.md L1+), add them here.
add_library(arcade_core STATIC
  src/QrCode.cpp
  src/core/CoreSmoke.cpp
)

target_include_directories(arcade_core PUBLIC src)

if(MSVC)
  target_compile_options(arcade_core PRIVATE /W3)
else()
  target_compile_options(arcade_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ── core_selfcheck ───────────────────────────────────────────────────────────
# Tiny driver that runs the portable-core self-check. Returns non-zero on any
# failure so CI can gate on it.
add_executable(core_selfcheck src/core/SelfCheckMain.cpp)
target_link_libraries(core_selfcheck PRIVATE arcade_core)

enable_testing()
add_test(NAME core_selfcheck COMMAND core_selfcheck)
