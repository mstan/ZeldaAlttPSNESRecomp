cmake_minimum_required(VERSION 3.22)
project(ZeldaAlttpAndroid C CXX)

# Android native build for ZeldaALttPSNESRecomp.
#
# Produces libmain.so, loaded by GameActivity (an SDLActivity subclass). It
# links the shared snesrecomp runtime, the Zelda game runtime and SDL3 (static,
# built from the pinned release). The ImGui launcher, the desktop OpenGL
# presenter and the mod catalog are desktop-only: Android has its own Java
# launcher and presents through SDL_Renderer (GLES).
#
# The generated recompiler output (src/gen) is derived from the user's ROM and
# is never committed or distributed. ZELDA_GEN_DIR points at it.

set(CMAKE_C_STANDARD 11)
set(ZELDA_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(ZELDA_ROOT "${ZELDA_ROOT}" ABSOLUTE)

set(SNESRECOMP_BUILD_VERSION "android-dev" CACHE STRING "Version reported in diagnostics")
# Desktop launcher + mod packages are not used on Android (see header).
set(SNESRECOMP_ENABLE_MODS OFF CACHE BOOL "" FORCE)
set(SNESRECOMP_ENABLE_TRACE OFF CACHE BOOL "" FORCE)
set(SNESRECOMP_SDL_BACKEND SDL3 CACHE STRING "" FORCE)
set(SNESRECOMP_ROOT "${ZELDA_ROOT}/snesrecomp" CACHE PATH "snesrecomp checkout")
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# ZELDA_INTERP_ONLY (default, distributable): no code or data derived from the
# game is compiled in. The player's ROM runs on the snesrecomp interpreter
# floor through jni/zelda_interp_module.c.
# OFF (local testing only, never distribute): link the AOT tree generated from
# your own ROM by tools/regen.sh into src/gen.
option(ZELDA_INTERP_ONLY "Build without generated game code (interpreter floor)" ON)
if(ZELDA_INTERP_ONLY)
    set(ZELDA_PROGRAM_SOURCES ${CMAKE_CURRENT_LIST_DIR}/jni/zelda_interp_module.c)
    set(ZELDA_SCHEDULER_IMPL "LLE" CACHE STRING "Main-loop scheduler (HLE or LLE)" FORCE)
    message(STATUS "Zelda Android: interpreter-only build (no generated game code)")
else()
    set(ZELDA_GEN_DIR "${ZELDA_ROOT}/src/gen" CACHE PATH "Generated program directory")
    file(GLOB ZELDA_PROGRAM_SOURCES CONFIGURE_DEPENDS ${ZELDA_GEN_DIR}/*.c)
    if(NOT ZELDA_PROGRAM_SOURCES)
        message(FATAL_ERROR
            "src/gen/ is empty: run `bash tools/regen.sh` with your own verified "
            "zelda.sfc at the repo root first (see android/README.md).")
    endif()
    message(WARNING "Zelda Android: AOT build links ROM-derived code. Local testing only.")
endif()

add_library(main SHARED
    ${SNESRECOMP_RUNNER_SOURCES}
    ${ZELDA_ROOT}/src/config.c
    ${ZELDA_ROOT}/src/main.c
    ${ZELDA_ROOT}/src/zelda_00.c
    ${ZELDA_ROOT}/src/gen_stubs.c
    ${ZELDA_ROOT}/src/zelda_cpu_infra.c
    ${ZELDA_ROOT}/src/zelda_world.c
    ${ZELDA_ROOT}/src/zelda_rtl.c
    ${ZELDA_ROOT}/src/zelda_spc_player.c
    ${SNESRECOMP_RUNNER_ROOT}/src/desktop/android_host.c
    ${SNESRECOMP_RUNNER_ROOT}/src/desktop/android_present.c
    ${CMAKE_CURRENT_LIST_DIR}/jni/zelda_android_jni.c
    ${ZELDA_PROGRAM_SOURCES}
)

set(ZELDA_SCHEDULER_IMPL "HLE" CACHE STRING "Main-loop scheduler (HLE or LLE)")  # AOT default
set_property(SOURCE ${ZELDA_ROOT}/src/zelda_rtl.c APPEND PROPERTY COMPILE_DEFINITIONS
    ZELDA_SCHEDULER_LLE=$<STREQUAL:${ZELDA_SCHEDULER_IMPL},LLE>)
set_property(SOURCE ${ZELDA_ROOT}/src/main.c APPEND PROPERTY COMPILE_DEFINITIONS
    SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}")

target_include_directories(main PRIVATE
    ${ZELDA_ROOT}/src
    ${ZELDA_ROOT}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SNESRECOMP_RUNNER_ROOT}/src/desktop
)

snesrecomp_target_sdl(main)
snesrecomp_target_post_mortem(main)

target_compile_definitions(main PRIVATE
    SMW_DISABLE_LM=1
    SYSTEM_VOLUME_MIXER_AVAILABLE=0
    SNESRECOMP_TRACE=0
    SNESRECOMP_REVERSE_DEBUG=0
)
# The recompiled C is machine-generated; quiet its warnings.
target_compile_options(main PRIVATE -w -Wno-implicit-function-declaration -O2)

target_link_libraries(main PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES} m log android)
