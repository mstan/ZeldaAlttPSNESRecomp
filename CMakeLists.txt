cmake_minimum_required(VERSION 3.16)
project(ZeldaALttPSNESRecomp C)

# Cross-platform (macOS/Linux) build for LegendofZeldaAlttpRecomp.
#
# The canonical Windows build is the MSVC solution (zelda.sln). This CMake build
# mirrors the same source list (see snesrecomp/runner/runner.cmake) so the game
# builds natively on macOS/Linux with clang/gcc + SDL2 + OpenGL. Modeled on the
# MegamanXRecomp CMakeLists; Zelda uses no Win32 Fiber API, so no fiber shim.
#
# Prerequisites: a snesrecomp checkout at ./snesrecomp (symlink/clone of the
# sibling repo), the generated C in src/gen/ (run `bash tools/regen.sh` with a
# verified zelda.sfc at the repo root), SDL2 (e.g. `apt install libsdl2-dev`).

set(CMAKE_C_STANDARD 11)

# Widescreen is a mod package, not a Display setting (same migration as Mega Man
# X / X2 / Super Mario World / Super Mario Kart). Must be set BEFORE
# runner.cmake is included: the engine forces RECOMP_UI_ENABLE_MODS on from this
# and compiles the package loader + trusted-plugin runtime, which is what makes
# the launcher's Mods view appear at all.
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL
    "Enable A Link to the Past's built-in mod catalog and SNES mod packages" FORCE)

set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# Launcher UI backend: the SHARED recomp-ui launcher (F:\Projects\recomp-ui,
# consumed as the ./recomp-ui submodule/junction) so every recomp game drives
# ONE launcher repo. Mirrors MegamanXRecomp/StarFoxSNESRecomp.
include(${CMAKE_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)

find_package(OpenGL REQUIRED)

# Generated recompiler output (produced by tools/regen.sh; not committed).
file(GLOB ZELDA_GEN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gen/*.c)
if(NOT ZELDA_GEN_SOURCES)
    message(FATAL_ERROR
        "src/gen/ is empty — run `bash tools/regen.sh` with a verified zelda.sfc "
        "at the repo root before building.")
endif()

add_executable(ZeldaALttPSNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    # game-specific runtime
    src/config.c
    src/main.c
    src/opengl.c
    # Trusted static plugin backing the widescreen mod package's activation.
    src/mods/zelda_widescreen_plugin.c
    src/zelda_00.c
    src/gen_stubs.c
    src/zelda_cpu_infra.c
    src/zelda_rtl.c
    src/zelda_spc_player.c

    # OpenGL function loader
    third_party/gl_core/gl_core_3_1.c
    # recompiled banks (generated)
    ${ZELDA_GEN_SOURCES}
)

# Dear ImGui pre-boot launcher (GUI) from the shared recomp-ui repo. One call
# wires sources/includes/RECOMP_LAUNCHER define/CXX/asset-staging (same Zelda
# boxart; recomp-ui's default pad.tga is already the SNES pad).
# CONSOLE is required by recomp-ui: it selects exactly one validated
# assets/consoles/<id>/ manifest, so an SNES game cannot silently stage N64
# cartridge or NES pad art next to the exe.
recomp_target_launcher_ui(ZeldaALttPSNESRecomp
    CONSOLE snes
    BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)

target_include_directories(ZeldaALttPSNESRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
)
# SDL3 by default, SDL2 via -DSNESRECOMP_SDL_BACKEND=SDL2. Replaces the game's
# own find_package(SDL2)/SDL2_LIBRARIES wiring: recomp-ui now compiles its SDL3
# backend unconditionally, so hardcoding SDL2 here put two SDL majors in one
# binary. No SDLmain either way (Zelda provides a plain main()).
snesrecomp_target_sdl(ZeldaALttPSNESRecomp)
snesrecomp_target_post_mortem(ZeldaALttPSNESRecomp)
snesrecomp_target_glsl_shader(ZeldaALttPSNESRecomp)

# prod-vs-debug via the single SNESRECOMP_ENABLE_TRACE switch (runner.cmake,
# default OFF). Prod: no debug_server.c / TCP server / rings. Debug:
# -DSNESRECOMP_ENABLE_TRACE=ON. tools/build-linux.sh --config {prod,debug} sets it.
if(SNESRECOMP_ENABLE_TRACE)
    set(_ZELDA_TRACE 1)
else()
    set(_ZELDA_TRACE 0)
endif()
target_compile_definitions(ZeldaALttPSNESRecomp PRIVATE
    SMW_DISABLE_LM=1
    SYSTEM_VOLUME_MIXER_AVAILABLE=0   # Win32 WASAPI volume control excluded
    SNESRECOMP_TRACE=${_ZELDA_TRACE}  # TCP debug server: 0 in prod, 1 in debug
    SNESRECOMP_REVERSE_DEBUG=0        # raw WRAM stores; matches non-debug generated C
)

# The recompiled C is machine-generated; quiet its warnings and tolerate the
# K&R-style implicit declarations clang rejects by default.
if(NOT MSVC)
    target_compile_options(ZeldaALttPSNESRecomp PRIVATE
        -w -Wno-implicit-function-declaration)
endif()

# SDL is linked by snesrecomp_target_sdl() above, for whichever major is chosen.
target_link_libraries(ZeldaALttPSNESRecomp PRIVATE
    OpenGL::GL
)

if(WIN32)
    # Trace/debug-server sockets and post-mortem symbolization.
    target_link_libraries(ZeldaALttPSNESRecomp PRIVATE ws2_32)
endif()

# Release-owned mod catalog staged next to the exe. mod_runtime resolves "mods"
# against the anchored cwd (the exe dir on Windows, the .AppImage's folder on
# Linux), so the catalog has to sit beside the binary rather than in the source
# tree. Every feature stays disabled until the player enables it in Mods.
set(ZELDA_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(EXISTS "${ZELDA_PRELOADED_MODS}/packages")
    add_custom_command(TARGET ZeldaALttPSNESRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ZELDA_PRELOADED_MODS}"
            "$<TARGET_FILE_DIR:ZeldaALttPSNESRecomp>/mods"
        COMMENT "Preloading the A Link to the Past mod catalog")
endif()
