# Continuum 93 - C++/SDL3 port. One static executable, no runtime DLLs.
cmake_minimum_required(VERSION 3.24)

# Static CRT (/MT) for us and for SDL: the exe must run on a bare Windows install.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Continuum93 VERSION 3.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(C93_APP "Build the SDL app (OFF: CPU core + conformance runner only, no download)" ON)

# Web (Emscripten): every object, SDL and the codecs included, needs shared memory for the CPU and APU
# threads; the assemblers and compilers throw, so native wasm exceptions; musttail needs wasm tail calls.
if(EMSCRIPTEN)
  add_compile_options(-pthread -fwasm-exceptions -mtail-call)
  # The machine is ~30 MB; the compilers and the C boot program want stack.
  add_link_options(-pthread -fwasm-exceptions -mtail-call
    -sINITIAL_MEMORY=256MB -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=4MB -sDEFAULT_PTHREAD_STACK_SIZE=2MB)
endif()

# --- SDL3, static, trimmed to what an 8-layer retro machine needs -------------
if(C93_APP)
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.4.20
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3)

# --- Ogg Opus decoding (DECISIONS D-6): libogg + libopus, static, library only ------------------
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)   # older cmake_minimum_required in the deps must still honour /MT
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_STATIC_RUNTIME ON CACHE BOOL "" FORCE)   # opus picks its own CRT: /MT like the rest
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ogg
  GIT_REPOSITORY https://github.com/xiph/ogg.git
  GIT_TAG v1.3.6
  GIT_SHALLOW TRUE)
FetchContent_Declare(opus
  GIT_REPOSITORY https://github.com/xiph/opus.git
  GIT_TAG v1.5.2
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ogg opus)

# --- The emulator --------------------------------------------------------------
# Service mode: every tool window is its own file there and registers itself (docs/SERVICE.md).
file(GLOB C93_SERVICE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/service/*.cpp)
add_executable(continuum WIN32
  ${C93_SERVICE_SOURCES}
  src/main.cpp
  src/tools.cpp
  src/log.cpp
  src/audio.cpp
  src/audio_xsound.cpp
  src/asm.cpp
  src/basic.cpp
  src/cc_gen.cpp
  src/cc_parse.cpp
  src/cc_pp.cpp
  src/cpu.cpp
  src/ints.cpp
  src/ints_fs.cpp
  src/ints_input.cpp
  src/ints_text.cpp
  src/ints_video.cpp
  src/video.cpp)
target_include_directories(continuum PRIVATE src ${CMAKE_BINARY_DIR}/gen)
target_link_libraries(continuum PRIVATE SDL3::SDL3-static ogg opus)

# App icon: Windows embeds icon/continuum.ico as a resource; elsewhere the window gets
# icon/window.png, compiled in as a byte array (no file to ship or lose).
if(WIN32)
  target_sources(continuum PRIVATE src/continuum.rc)
else()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/icon/window.png)
  file(READ ${CMAKE_SOURCE_DIR}/icon/window.png C93_ICON_HEX HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," C93_ICON_BYTES "${C93_ICON_HEX}")
  file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/gen/icon_png.inc CONTENT "${C93_ICON_BYTES}\n" @ONLY)
endif()
target_compile_definitions(continuum PRIVATE C93_VERSION="${PROJECT_VERSION}" C93_AUDIO_OUT=1)

if(MSVC)
  # /GS- and /Gy: the interpreter loop is hot and has no untrusted stack buffers.
  target_compile_definitions(continuum PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(continuum PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus
    $<$<CONFIG:Release>:/O2 /Ob3 /Oi /GL /GS- /Gy /Gw>)
  target_link_options(continuum PRIVATE
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
  # Debug builds keep a console so the log mirror is visible.
  set_target_properties(continuum PROPERTIES WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
else()
  # GCC/Clang on Linux, macOS, Raspberry Pi. No -march: the binary must run on any CPU of its arch.
  # -ffp-contract=off: no fused multiply-add (GCC's default on ARM), so float math matches .NET bit for bit.
  target_compile_options(continuum PRIVATE -Wall -Wextra -ffp-contract=off $<$<CONFIG:Release>:-O3 -flto>)
  target_link_options(continuum PRIVATE $<$<CONFIG:Release>:-flto>)
  if(NOT APPLE AND NOT EMSCRIPTEN)
    # glibc stays dynamic (static glibc breaks NSS/dlopen); the C++ runtime is baked in.
    target_link_options(continuum PRIVATE -static-libstdc++ -static-libgcc $<$<CONFIG:Release>:-s>)
  endif()
endif()

if(EMSCRIPTEN)
  # continuum.html/.js/.wasm/.data: Data/ is packed into the .data file and mounted at /Data
  # (SDL_GetBasePath() is "/").
  # shortcut: files the guest saves live in memory and are gone on reload; mount IDBFS on /Data/filesystem to keep them.
  set_target_properties(continuum PROPERTIES SUFFIX ".html")
  target_link_options(continuum PRIVATE
    -sPTHREAD_POOL_SIZE=4
    --preload-file ${CMAKE_SOURCE_DIR}/Data@/Data
    --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html)
  set_property(TARGET continuum APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/shell.html)
else()
  # Data/ (OS sources, filesystem) sits next to the exe, as in the original.
  add_custom_command(TARGET continuum POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory   # not _if_different: that needs CMake 3.26
      ${CMAKE_SOURCE_DIR}/Data $<TARGET_FILE_DIR:continuum>/Data)
endif()
endif()

# --- Conformance runner (no SDL): replays tests/oracle vectors through the CPU core -------
add_executable(conformance tests/conformance.cpp src/service/disasm.cpp src/service/cpu_tracked.cpp src/audio.cpp src/audio_xsound.cpp src/asm.cpp src/basic.cpp src/cc_gen.cpp src/cc_parse.cpp src/cc_pp.cpp src/cpu.cpp src/ints.cpp src/ints_fs.cpp src/ints_input.cpp src/ints_text.cpp src/ints_video.cpp src/log.cpp)
if(EMSCRIPTEN)
  target_link_options(conformance PRIVATE -sNODERAWFS)   # runs under node and reads the vector files from disk
endif()
target_include_directories(conformance PRIVATE src)
if(MSVC)
  target_compile_definitions(conformance PRIVATE _CRT_SECURE_NO_WARNINGS)
  # Same optimisation flags as the app: the tests must check the code that ships (intrinsics, LTO).
  target_compile_options(conformance PRIVATE /W4 /utf-8 $<$<CONFIG:Release>:/O2 /Oi /GL /GS- /Gy /Gw>)
  target_link_options(conformance PRIVATE $<$<CONFIG:Release>:/LTCG>)
else()
  target_compile_options(conformance PRIVATE -ffp-contract=off $<$<CONFIG:Release>:-O3>)
endif()
