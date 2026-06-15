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
  src/Platform/Text.cpp
  src/Platform/Sha256.cpp
  src/Platform/Paths.cpp
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

# ── arcade_net + net_selfcheck (Phase L2) ────────────────────────────────────
# The libcurl-backed platform::IHttpClient and a headless driver that hits the
# server's /api/health. Linux/Unix only: the Windows build keeps its WinHTTP
# pump. Built only when CURL is available so a bare core checkout still configures.
if(NOT MSVC)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    set(ARCADE_NET_SRC src/Platform/linux/HttpClientCurl.cpp)

    # IXWebSocket for the social gateway (Phase L2b). Pulled via FetchContent so
    # the build is self-contained; needs OpenSSL (TLS) + zlib. Opt out with
    # -DARCADE_WITH_WS=OFF for a HTTP-only core build.
    option(ARCADE_WITH_WS "Build the IXWebSocket gateway client (L2b)" ON)
    if(ARCADE_WITH_WS)
      find_package(OpenSSL QUIET)
      if(OpenSSL_FOUND)
        include(FetchContent)
        set(USE_TLS ON CACHE BOOL "" FORCE)
        set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
        set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(ixwebsocket
          GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
          GIT_TAG        v11.4.5
          GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(ixwebsocket)
        list(APPEND ARCADE_NET_SRC src/Platform/linux/WebSocketIx.cpp)
      else()
        message(STATUS "OpenSSL not found — skipping IXWebSocket gateway (L2b).")
        set(ARCADE_WITH_WS OFF)
      endif()
    endif()

    add_library(arcade_net STATIC ${ARCADE_NET_SRC})
    target_include_directories(arcade_net PUBLIC src)
    target_link_libraries(arcade_net PUBLIC CURL::libcurl)
    target_compile_options(arcade_net PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(ARCADE_WITH_WS)
      target_link_libraries(arcade_net PUBLIC ixwebsocket)
    endif()

    add_executable(net_selfcheck src/core/NetSelfCheckMain.cpp)
    target_link_libraries(net_selfcheck PRIVATE arcade_net arcade_core)
    add_test(NAME net_selfcheck COMMAND net_selfcheck)

    if(ARCADE_WITH_WS)
      add_executable(ws_selfcheck src/core/WsSelfCheckMain.cpp)
      target_link_libraries(ws_selfcheck PRIVATE arcade_net arcade_core)
      add_test(NAME ws_selfcheck COMMAND ws_selfcheck)
    endif()
  else()
    message(STATUS "CURL not found — skipping arcade_net/net_selfcheck (L2).")
  endif()

  # ── arcade_gui + gui_smoke (Phase L3a) ──────────────────────────────────────
  # SDL2-backed platform::IWindow (window + GL context + event loop) and a smoke
  # app that opens a window, renders a known clear color, reads the framebuffer
  # back, and dumps gui_smoke.ppm. nanovg Renderer2D + the grid renderer land in
  # L3b. Built only when SDL2 + OpenGL are present.
  find_package(SDL2 QUIET)
  find_package(OpenGL QUIET)
  if(SDL2_FOUND AND OpenGL_FOUND)
    add_library(arcade_gui STATIC
      src/Platform/linux/WindowSdl.cpp
    )
    target_include_directories(arcade_gui PUBLIC src ${SDL2_INCLUDE_DIRS})
    target_link_libraries(arcade_gui PUBLIC ${SDL2_LIBRARIES} OpenGL::GL)
    target_compile_options(arcade_gui PRIVATE -Wall -Wextra -Wno-unused-parameter)

    add_executable(gui_smoke src/core/GuiSmokeMain.cpp)
    target_link_libraries(gui_smoke PRIVATE arcade_gui arcade_core)
    add_test(NAME gui_smoke COMMAND gui_smoke)
  else()
    message(STATUS "SDL2/OpenGL not found — skipping arcade_gui/gui_smoke (L3).")
  endif()
endif()

enable_testing()
add_test(NAME core_selfcheck COMMAND core_selfcheck)
