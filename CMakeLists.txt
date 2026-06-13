cmake_minimum_required(VERSION 3.16)
project(ZeldaAlttPSNESRecomp C)

# Cross-platform (macOS/Linux) build for ZeldaAlttPSNESRecomp.
#
# The canonical Windows build is the MSVC project (zelda.vcxproj). This CMake
# build mirrors the same source list (see snesrecomp/runner/runner.cmake) so the
# game builds natively on macOS/Linux with clang/gcc + SDL2 + OpenGL.
#
# Prerequisites: a snesrecomp checkout at ./snesrecomp (symlink/clone of the
# sibling repo), the generated C in src/gen/ (run the v2 regen tools with a
# verified zelda.sfc), SDL2 (e.g. `brew install sdl2`).

set(CMAKE_C_STANDARD 11)

set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# Generated recompiler output (produced by the v2 regen tools; not committed).
file(GLOB ZELDA_GEN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gen/*.c)
if(NOT ZELDA_GEN_SOURCES)
    message(FATAL_ERROR
        "src/gen/ is empty — run the v2 regen tools with a verified zelda.sfc "
        "before building.")
endif()

add_executable(ZeldaAlttPSNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    # game-specific runtime
    src/config.c
    src/glsl_shader.c
    src/main.c
    src/post_mortem.c
    src/opengl.c
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

target_include_directories(ZeldaAlttPSNESRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(ZeldaAlttPSNESRecomp PRIVATE
    SMW_DISABLE_LM=1
    SYSTEM_VOLUME_MIXER_AVAILABLE=0   # Win32 WASAPI volume control excluded
    SNESRECOMP_TRACE=0                # no TCP debug server (debug_server.c excluded)
    SNESRECOMP_REVERSE_DEBUG=0        # raw WRAM stores; matches non-debug generated C
)

# The recompiled C is machine-generated; keep its warnings from drowning the
# build, and tolerate the K&R-style implicit declarations clang rejects by
# default.
if(NOT MSVC)
    target_compile_options(ZeldaAlttPSNESRecomp PRIVATE
        -w -Wno-implicit-function-declaration)
endif()

target_link_libraries(ZeldaAlttPSNESRecomp PRIVATE
    ${SDL2_LIBRARIES}
    OpenGL::GL
)
